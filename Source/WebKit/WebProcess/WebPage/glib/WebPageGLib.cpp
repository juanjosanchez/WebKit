/*
 * Copyright (C) 2019 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "WebPage.h"

#include "DrawingAreaCoordinatedGraphicsGLib.h"
#include "EditorState.h"
#include "InputMethodState.h"
#include "LoginForm.h"
#include "MessageSenderInlines.h"
#include "RenderProcessInfo.h"
#include "UserMessage.h"
#include "WebFrame.h"
#include "WebKitUserMessage.h"
#include "WebKitWebPagePrivate.h"
#include "WebPageProxyMessages.h"
#include "WebProcess.h"
#include "WebProcessExtensionManager.h"
#include <WebCore/Autofill.h>
#include <WebCore/AutofillElements.h>
#include <WebCore/DocumentView.h>
#include <WebCore/Editor.h>
#include <WebCore/ElementInlines.h>
#include <WebCore/FocusController.h>
#include <WebCore/FormListedElement.h>
#include <WebCore/FrameSelection.h>
#include <WebCore/HTMLFormElement.h>
#include <WebCore/HTMLInputElement.h>
#include <WebCore/HTMLNames.h>
#include <WebCore/HTMLTextAreaElement.h>
#include <WebCore/LocalFrameInlines.h>
#include <WebCore/LocalFrameView.h>
#include <WebCore/NodeDocument.h>
#include <WebCore/NodeList.h>
#include <WebCore/Page.h>
#include <WebCore/PlatformDisplay.h>
#include <WebCore/PointerCharacteristics.h>
#include <WebCore/Range.h>
#include <WebCore/Settings.h>
#include <WebCore/Text.h>
#include <WebCore/TextIterator.h>
#include <WebCore/UserAgent.h>
#include <WebCore/VisiblePosition.h>
#include <WebCore/VisibleUnits.h>
#include <wtf/text/MakeString.h>

#if ENABLE(2022_GLIB_API)
#include "WebKitWebProcessExtension.h"
#else
#include "WebKitWebExtension.h"
#endif

#if USE(GBM)
#include <WebCore/DRMDeviceManager.h>
#include <WebCore/GBMDevice.h>
#include <WebCore/MemoryMappedGPUBuffer.h>
#include <gbm.h>
#include <xf86drm.h>
#endif

namespace WebKit {
using namespace WebCore;

void WebPage::platformInitialize(const WebPageCreationParameters&)
{
#if USE(ATSPI)
    // Create the accessible object (the plug) that will serve as the
    // entry point to the Web process, and send a message to the UI
    // process to connect the two worlds through the accessibility
    // object there specifically placed for that purpose (the socket).
    if (RefPtr page = corePage()) {
        lazyInitialize(m_accessibilityRootObject, AccessibilityRootAtspi::create(*page));
        m_accessibilityRootObject->registerObject([&](const String& plugID) {
            if (!plugID.isEmpty())
                send(Messages::WebPageProxy::BindAccessibilityTree(plugID));
        });
    }
#endif
}

void WebPage::platformDetach()
{
#if USE(ATSPI)
    if (m_accessibilityRootObject)
        m_accessibilityRootObject->unregisterObject();
#endif
}

void WebPage::sendMessageToWebProcessExtensionWithReply(UserMessage&& message, CompletionHandler<void(UserMessage&&)>&& completionHandler)
{
    auto* extension = WebProcessExtensionManager::singleton().extension();
    if (!extension) {
        completionHandler(UserMessage(message.name, WEBKIT_USER_MESSAGE_UNHANDLED_MESSAGE));
        return;
    }

#if ENABLE(2022_GLIB_API)
    auto* page = webkit_web_process_extension_get_page(extension, m_identifier.toUInt64());
#else
    auto* page = webkit_web_extension_get_page(extension, m_identifier.toUInt64());
#endif
    if (!page) {
        completionHandler(UserMessage(message.name, WEBKIT_USER_MESSAGE_UNHANDLED_MESSAGE));
        return;
    }

    webkitWebPageDidReceiveUserMessage(page, WTF::move(message), WTF::move(completionHandler));
}

void WebPage::sendMessageToWebProcessExtension(UserMessage&& message)
{
    sendMessageToWebProcessExtensionWithReply(WTF::move(message), [](UserMessage&&) { });
}

static RefPtr<LocalFrame> targetFrameForEditing(WebPage& page)
{
    RefPtr targetFrame = page.corePage()->focusController().focusedOrMainFrame();
    if (!targetFrame)
        return nullptr;

    Editor& editor = targetFrame->editor();
    if (!editor.canEdit())
        return nullptr;

    if (editor.hasComposition()) {
        // We should verify the parent node of this IME composition node are
        // editable because JavaScript may delete a parent node of the composition
        // node. In this case, WebKit crashes while deleting texts from the parent
        // node, which doesn't exist any longer.
        if (auto range = editor.compositionRange()) {
            if (!range->startContainer().isContentEditable())
                return nullptr;
        }
    }
    return targetFrame;
}

void WebPage::cancelComposition(const String& compositionString)
{
    if (RefPtr targetFrame = targetFrameForEditing(*this))
        protect(targetFrame->editor())->confirmComposition(compositionString);
}

enum class SurrogatePairDirection : bool { Backward, Forward };

// A boundary found by counting UTF-16 code units, as input methods do, can split a surrogate pair.
static BoundaryPoint moveOutOfSurrogatePair(const BoundaryPoint& point, SurrogatePairDirection direction)
{
    RefPtr text = dynamicDowncast<Text>(point.container);
    if (!text || !point.offset || point.offset >= text->length())
        return point;
    auto& data = text->data();
    if (!U16_IS_LEAD(data[point.offset - 1]) || !U16_IS_TRAIL(data[point.offset]))
        return point;
    return { point.container.copyRef(), direction == SurrogatePairDirection::Backward ? point.offset - 1 : point.offset + 1 };
}

void WebPage::deleteSurrounding(int64_t offset, unsigned characterCount)
{
    RefPtr targetFrame = targetFrameForEditing(*this);
    if (!targetFrame)
        return;

    auto& selection = targetFrame->selection().selection();
    if (selection.isNone())
        return;

    auto selectionStart = selection.visibleStart();
    auto surroundingStart = startOfEditableContent(selectionStart);
    auto surroundingRange = makeSimpleRange(surroundingStart, endOfEditableContent(selectionStart));
    auto cursorPositionRange = makeSimpleRange(surroundingStart, selectionStart);
    if (!surroundingRange || !cursorPositionRange)
        return;

    auto cursorPosition = WebCore::characterCount(*cursorPositionRange);
    if (offset < -static_cast<int64_t>(cursorPosition))
        return;

    auto characterRange = WebCore::CharacterRange(cursorPosition + offset, characterCount);
    auto deletionRange = resolveCharacterRange(*surroundingRange, characterRange);

    CheckedRef frameSelection = targetFrame->selection();
    targetFrame->editor().setIgnoreSelectionChanges(true);
    if (deletionRange.collapsed()) {
        auto caret = moveOutOfSurrogatePair(deletionRange.start, SurrogatePairDirection::Forward);
        frameSelection->setSelection(VisibleSelection(VisiblePosition(makeDeprecatedLegacyPosition(caret))));
    } else {
        // Delete exactly the requested range. An input method can delete a part of a grapheme cluster, like a combining
        // mark, which a validated selection would extend to the end of the cluster.
        VisibleSelection selectionToDelete;
        selectionToDelete.setWithoutValidation(makeDeprecatedLegacyPosition(moveOutOfSurrogatePair(deletionRange.start, SurrogatePairDirection::Backward)),
            makeDeprecatedLegacyPosition(moveOutOfSurrogatePair(deletionRange.end, SurrogatePairDirection::Forward)));
        frameSelection->setSelection(selectionToDelete);
        targetFrame->editor().deleteSelectionWithSmartDelete(false);
        // Don't leave the range selected when nothing could be deleted.
        if (!frameSelection->selection().isCaret())
            frameSelection->setSelection(VisibleSelection(frameSelection->selection().visibleStart()));
    }
    targetFrame->editor().setIgnoreSelectionChanges(false);
    sendEditorStateUpdate();
}

// The input method only uses the text near the selection, and sending the whole editable content would
// take time linear in its length on every change. The Wayland text-input protocols truncate it to 4000 bytes anyway.
static constexpr unsigned maxSurroundingTextLength = 1000;

static BoundaryPoint surroundingTextStart(const BoundaryPoint& editableStart, const BoundaryPoint& position)
{
    // An empty range that starts before a child node is not handled by BackwardsCharacterIterator:
    // it walks out of the editable content, through the text before it.
    if (editableStart == position)
        return editableStart;

    BackwardsCharacterIterator iterator({ editableStart, position });
    if (!iterator.atEnd())
        iterator.advance(maxSurroundingTextLength);
    if (iterator.atEnd())
        return editableStart;
    return moveOutOfSurrogatePair(iterator.range().end, SurrogatePairDirection::Forward);
}

static BoundaryPoint surroundingTextEnd(const BoundaryPoint& position, const BoundaryPoint& editableEnd)
{
    return moveOutOfSurrogatePair(resolveCharacterLocation({ position, editableEnd }, maxSurroundingTextLength), SurrogatePairDirection::Backward);
}

void WebPage::getPlatformEditorState(LocalFrame& frame, EditorState& result) const
{
    if (!result.hasPostLayoutAndVisualData() || !frame.view() || frame.view()->needsLayout())
        return;

    auto& postLayoutData = *result.postLayoutData;
    auto& visualData = *result.visualData;
    visualData.caretRectAtStart = frame.selection().absoluteCaretBounds();

    const VisibleSelection& selection = frame.selection().selection();
    if (selection.isNone())
        return;

#if PLATFORM(GTK)
    const Editor& editor = frame.editor();
    if (selection.isRange()) {
        if (editor.selectionHasStyle(CSSPropertyID::FontWeight, "bold"_s) == TriState::True)
            postLayoutData.typingAttributes.add(TypingAttribute::Bold);
        if (editor.selectionHasStyle(CSSPropertyID::FontStyle, "italic"_s) == TriState::True)
            postLayoutData.typingAttributes.add(TypingAttribute::Italics);
        if (editor.selectionHasStyle(CSSPropertyID::WebkitTextDecorationsInEffect, "underline"_s) == TriState::True)
            postLayoutData.typingAttributes.add(TypingAttribute::Underline);
        if (editor.selectionHasStyle(CSSPropertyID::WebkitTextDecorationsInEffect, "line-through"_s) == TriState::True)
            postLayoutData.typingAttributes.add(TypingAttribute::StrikeThrough);
    } else if (selection.isCaret()) {
        if (editor.selectionStartHasStyle(CSSPropertyID::FontWeight, "bold"_s))
            postLayoutData.typingAttributes.add(TypingAttribute::Bold);
        if (editor.selectionStartHasStyle(CSSPropertyID::FontStyle, "italic"_s))
            postLayoutData.typingAttributes.add(TypingAttribute::Italics);
        if (editor.selectionStartHasStyle(CSSPropertyID::WebkitTextDecorationsInEffect, "underline"_s))
            postLayoutData.typingAttributes.add(TypingAttribute::Underline);
        if (editor.selectionStartHasStyle(CSSPropertyID::WebkitTextDecorationsInEffect, "line-through"_s))
            postLayoutData.typingAttributes.add(TypingAttribute::StrikeThrough);
    }
#endif

    if (selection.isContentEditable()) {
        auto selectionStart = selection.visibleStart();
        auto editableRange = makeSimpleRange(startOfEditableContent(selectionStart), endOfEditableContent(selectionStart));
        auto compositionRange = frame.editor().compositionRange();
        if (editableRange && compositionRange && contains<ComposedTree>(*editableRange, *compositionRange)) {
            auto surroundingStart = surroundingTextStart(editableRange->start, compositionRange->start);
            auto surroundingEnd = surroundingTextEnd(compositionRange->end, editableRange->end);
            auto beforeText = plainText({ surroundingStart, compositionRange->start });
            postLayoutData.surroundingContext = makeString(beforeText, plainText({ compositionRange->end, surroundingEnd }));
            postLayoutData.surroundingContextCursorPosition = beforeText.length();
            postLayoutData.surroundingContextSelectionPosition = postLayoutData.surroundingContextCursorPosition;
        } else if (auto selectionRange = makeSimpleRange(selectionStart, selection.visibleEnd()); editableRange && selectionRange) {
            auto surroundingStart = surroundingTextStart(editableRange->start, selectionRange->start);
            auto surroundingEnd = surroundingTextEnd(selectionRange->end, editableRange->end);
            postLayoutData.surroundingContext = plainText({ surroundingStart, surroundingEnd });
            postLayoutData.surroundingContextCursorPosition = characterCount({ surroundingStart, selectionRange->start });
            postLayoutData.surroundingContextSelectionPosition = characterCount({ surroundingStart, selectionRange->end });
        } else
            postLayoutData.surroundingContext = emptyString();
    }
}

static std::optional<InputMethodState> inputMethodStateForElement(Element* element)
{
    if (!element || !element->shouldUseInputMethod())
        return std::nullopt;

    if (auto* textControl = dynamicDowncast<HTMLTextFormControlElement>(*element); textControl && textControl->isReadOnly())
        return std::nullopt;

    InputMethodState state;
    if (is<HTMLInputElement>(*element)) {
        auto& inputElement = downcast<HTMLInputElement>(*element);
        state.setPurposeForInputElement(inputElement);
        // A missing inputmode attribute keeps the purpose derived from the type. A password
        // field keeps its purpose too, because the state has no way to say "secret" separately.
        auto inputMode = inputElement.canonicalInputMode();
        if (inputMode == InputMode::None || (inputMode != InputMode::Unspecified && !inputElement.isPasswordField()))
            state.setPurposeOrHintForInputMode(inputMode);
        // A password revealed by changing the type of the field to text should not be learned either.
        if (inputElement.hasEverBeenPasswordField())
            state.hints.add(InputMethodState::Hint::Private);
#if ENABLE(AUTOCAPITALIZE)
        state.addHintsForAutocapitalizeType(inputElement.autocapitalizeType());
#endif
    } else if (is<HTMLTextAreaElement>(*element) || (element->hasEditableStyle() && is<HTMLElement>(*element))) {
        auto& htmlElement = downcast<HTMLElement>(*element);
        state.setPurposeOrHintForInputMode(htmlElement.canonicalInputMode());
#if ENABLE(AUTOCAPITALIZE)
        state.addHintsForAutocapitalizeType(htmlElement.autocapitalizeType());
#endif
    }

    // enterkeyhint applies to any HTML element, including a contenteditable host.
    if (auto* htmlElement = dynamicDowncast<HTMLElement>(*element))
        state.enterKeyHint = htmlElement->canonicalEnterKeyHint();

    if (element->isSpellCheckingEnabled())
        state.hints.add(InputMethodState::Hint::Spellcheck);

    return state;
}

void WebPage::setInputMethodState(Element* element)
{
    // Some pages call blur() and then focus() on the focused element in the same task. Hold the reset
    // back until the next run loop iteration, like ElementDidBlur on Cocoa, so that the embedder does not
    // see a focus out and a focus in when the element has the focus again by the end of the task.
    // See elementDidBlur().
    // FIXME: With site isolation, the reset can now reach the UI process after the state sent by the
    // process of a remote frame that got the focus, and the UI process keeps the last state it gets.
    if (!element && m_recentlyBlurredElement)
        return;

    m_focusedElementBeforePageBlur = nullptr;
    sendInputMethodState(element, m_userIsInteracting ? InhibitOnScreenKeyboard::No : InhibitOnScreenKeyboard::Yes);
}

void WebPage::sendInputMethodState(Element* element, InhibitOnScreenKeyboard inhibitOnScreenKeyboard)
{
    sendFocusedLoginForm(element);

    auto state = inputMethodStateForElement(element);

    if (state && inhibitOnScreenKeyboard == InhibitOnScreenKeyboard::Yes)
        state->hints.add(InputMethodState::Hint::InhibitOnScreenKeyboard);

    if (m_inputMethodState == state)
        return;

    m_inputMethodState = state;
    send(Messages::WebPageProxy::SetInputMethodState(state));
}

void WebPage::resetInputMethodStateAfterBlur(Element& element)
{
    bool isPageBlur = !windowAndWebPageAreFocused() && protect(element.document())->focusedElement() == &element;
    m_focusedElementBeforePageBlur = isPageBlur && m_inputMethodState ? &element : nullptr;
    sendInputMethodState(nullptr, InhibitOnScreenKeyboard::No);
}

void WebPage::restoreInputMethodStateAfterPageFocus()
{
    if (!m_focusedElementBeforePageBlur || !windowAndWebPageAreFocused())
        return;

    RefPtr element = std::exchange(m_focusedElementBeforePageBlur, nullptr);
    if (protect(element->document())->focusedElement() != element)
        return;

    sendInputMethodState(element.get(), InhibitOnScreenKeyboard::Yes);

    // The UI process forgot the cursor area and the surrounding text when the view lost the focus. The selection did not
    // change, so nothing else is sure to send an editor state.
    scheduleFullEditorStateUpdate();
}

void WebPage::focusedElementDidChangeReadOnly(Element& element)
{
    if (m_focusedElement != &element)
        return;

    setInputMethodState(&element);
}

void WebPage::focusedElementDidChangeType(Element& element)
{
    if (m_focusedElement != &element)
        return;

    setInputMethodState(&element);
}

String WebPage::platformUserAgent(const URL& url) const
{
    if (url.isNull() || !m_page->settings().needsSiteSpecificQuirks())
        return String();

    return WebCore::standardUserAgentForURL(url);
}

bool WebPage::hoverSupportedByPrimaryPointingDevice() const
{
    return WebProcess::singleton().primaryPointingDevice() == AvailableInputDevices::Mouse;
}

bool WebPage::hoverSupportedByAnyAvailablePointingDevice() const
{
    return WebProcess::singleton().availableInputDevices().contains(AvailableInputDevices::Mouse);
}

std::optional<PointerCharacteristics> WebPage::pointerCharacteristicsOfPrimaryPointingDevice() const
{
    const auto& primaryPointingDevice = WebProcess::singleton().primaryPointingDevice();
    if (primaryPointingDevice == AvailableInputDevices::Mouse)
        return PointerCharacteristics::Fine;
    if (primaryPointingDevice == AvailableInputDevices::Touchscreen)
        return PointerCharacteristics::Coarse;
    return std::nullopt;
}

OptionSet<PointerCharacteristics> WebPage::pointerCharacteristicsOfAllAvailablePointingDevices() const
{
    OptionSet<PointerCharacteristics> pointerCharacteristics;
    const auto& availableInputs = WebProcess::singleton().availableInputDevices();
    if (availableInputs.contains(AvailableInputDevices::Mouse))
        pointerCharacteristics.add(PointerCharacteristics::Fine);
    if (availableInputs.contains(AvailableInputDevices::Touchscreen))
        pointerCharacteristics.add(PointerCharacteristics::Coarse);
    return pointerCharacteristics;
}

void WebPage::getRenderProcessInfo(CompletionHandler<void(RenderProcessInfo&&)>&& completionHandler)
{
    RenderProcessInfo info;

    auto* display = PlatformDisplay::sharedDisplayIfExists();
    if (!display) {
        completionHandler(WTF::move(info));
        return;
    }

    switch (display->type()) {
#if PLATFORM(GTK) || ENABLE(WPE_PLATFORM)
    case PlatformDisplay::Type::Surfaceless:
        info.platform = "Surfaceless"_s;
        break;
#if USE(GBM)
    case PlatformDisplay::Type::GBM:
        info.platform = "GBM"_s;
        if (auto device = DRMDeviceManager::singleton().mainGBMDevice(DRMDeviceManager::NodeType::Render)) {
            if (drmVersion* version = drmGetVersion(gbm_device_get_fd(device->device()))) {
                info.drmVersion = makeString(unsafeSpan(version->name), " ("_s, unsafeSpan(version->desc), ") "_s, version->version_major, '.', version->version_minor, '.', version->version_patchlevel, ". "_s, unsafeSpan(version->date));
                drmFreeVersion(version);
            }
        }
        break;
#endif
#endif
#if OS(ANDROID)
    case PlatformDisplay::Type::Android:
        info.platform = "Android"_s;
        break;
#endif
#if USE(WPE_RENDERER)
    case PlatformDisplay::Type::WPE:
        info.platform = "WPE"_s;
        break;
#endif
    default:
        break;
    }

    info.msaaSampleCount = display->msaaSampleCount();

#if USE(GBM)
    if (info.platform != "WPE"_s) {
        info.supportedBufferFormats = display->bufferFormats().map([](const auto& format) -> RendererBufferFormat::Format {
            return {
                format.fourcc.value,
                format.modifiers,
            };
        });
    }
    info.dmabufExportStrategy = MemoryMappedGPUBuffer::exportStrategyDescription();
    info.memoryMappedGPUBufferSupported = MemoryMappedGPUBuffer::isSupported();
#endif // USE(GBM)

    static_cast<DrawingAreaCoordinatedGraphics*>(m_drawingArea.get())->fillGLInformation(WTF::move(info), WTF::move(completionHandler));
}

// Labels and placeholders that a password manager may show, cut so that a page cannot make every message huge.
static constexpr unsigned maximumLoginFormFieldTextLength = 256;

// What a password field is for, from its autocomplete attribute, else from the form: a form with a second password
// field to fill is one to sign up or to change the password.
static LoginFormField::Role roleOfPasswordField(const HTMLInputElement& input, bool hasSecondPasswordField)
{
    switch (toAutofillFieldName(input.autofillData().fieldName)) {
    case AutofillFieldName::NewPassword:
        return LoginFormField::Role::NewPassword;
    case AutofillFieldName::CurrentPassword:
        return LoginFormField::Role::Password;
    default:
        return hasSecondPasswordField ? LoginFormField::Role::NewPassword : LoginFormField::Role::Password;
    }
}

std::optional<LoginForm> WebPage::loginFormForInput(HTMLInputElement& start, bool inputHasFocus)
{
    RefPtr coreFrame = start.document().frame();
    if (!coreFrame)
        return std::nullopt;
    RefPtr webFrame = WebFrame::fromCoreFrame(*coreFrame);
    if (!webFrame)
        return std::nullopt;
    auto elements = AutofillElements::computeAutofillElements(start);
    if (!elements)
        return std::nullopt;

    LoginForm form { webFrame->frameID(), { }, std::nullopt };
    Ref document = start.document();
    auto addField = [&](const RefPtr<HTMLInputElement>& input, LoginFormField::Role role) {
        // The focus order goes in and out of frames, but a field of another document is not part of the form: filling
        // it would give the logins of the site of one document to another.
        if (!input || &input->document() != document.ptr())
            return;
        auto context = contextForElement(*input);
        if (!context)
            return;
        if (inputHasFocus && input.get() == &start)
            form.focusedField = static_cast<uint32_t>(form.fields.size());
        String label;
        if (RefPtr labels = input->labels(); labels && labels->length()) {
            if (RefPtr first = labels->item(0))
                label = first->textContent().simplifyWhiteSpace(isASCIIWhitespace).left(maximumLoginFormFieldTextLength);
        }
        form.fields.append(LoginFormField {
            role,
            WTF::move(*context),
            input->value().get(),
            input->attributeWithoutSynchronization(HTMLNames::nameAttr).string(),
            input->getIdAttribute().string(),
            input->type().string(),
            input->attributeWithoutSynchronization(HTMLNames::autocompleteAttr).string(),
            input->attributeWithoutSynchronization(HTMLNames::placeholderAttr).string().left(maximumLoginFormFieldTextLength),
            WTF::move(label),
        });
    };

    auto password = elements->passwordField();
    auto secondPassword = elements->secondPasswordField();
    addField(elements->usernameField(), LoginFormField::Role::Username);
    if (password)
        addField(password, roleOfPasswordField(*password, secondPassword && &secondPassword->document() == document.ptr()));
    addField(secondPassword, LoginFormField::Role::NewPassword);

    // Without a password field left, only a field that says it takes a user name makes a login form.
    bool hasPasswordField = std::ranges::any_of(form.fields, [](auto& field) {
        return field.role != LoginFormField::Role::Username;
    });
    if (!hasPasswordField) {
        auto fieldName = toAutofillFieldName(start.autofillData().fieldName);
        if (form.fields.isEmpty() || (fieldName != AutofillFieldName::Username && fieldName != AutofillFieldName::WebAuthn))
            return std::nullopt;
    }
    return form;
}

// Tells the UI process of the login form of the focused field, if it has one, along with the input method state, at
// the same moments: when a field gets the focus, after a blur, and when the page gets the focus back.
void WebPage::sendFocusedLoginForm(Element* element)
{
    RefPtr input = dynamicDowncast<HTMLInputElement>(element);
    std::optional<LoginForm> form;
    if (m_loginFormAutofillEnabled && input && (input->isTextField() || input->isEmailField()))
        form = loginFormForInput(*input, true);
    m_focusedLoginFormInput = form ? input.get() : nullptr;
    if (loginFormsAreEqual(m_focusedLoginForm, form))
        return;
    m_focusedLoginForm = WTF::move(form);
    send(Messages::WebPageProxy::FocusedLoginFormChanged(m_focusedLoginForm));
}

void WebPage::updateFocusedLoginForm()
{
    // A form whose field went away without a blur is gone too.
    if (m_focusedLoginForm)
        sendFocusedLoginForm(m_focusedLoginFormInput.get());
}

void WebPage::scheduleFocusedLoginFormUpdate()
{
    // Fields move on every frame of a scroll, so their bounds go to the UI process at most this often.
    if (m_focusedLoginForm && !m_focusedLoginFormRefreshTimer.isActive())
        m_focusedLoginFormRefreshTimer.startOneShot(100_ms);
}

// Takes the bounds and values of the fields of the focused login form again, as after a scroll or a layout, without
// finding the fields again, unless one of them is gone.
void WebPage::refreshFocusedLoginForm()
{
    if (!m_focusedLoginForm)
        return;
    auto form = *m_focusedLoginForm;
    for (auto& field : form.fields) {
        RefPtr input = dynamicDowncast<HTMLInputElement>(elementForContext(field.element));
        if (!input) {
            updateFocusedLoginForm();
            return;
        }
        field.element.boundingRect = input->boundingBoxInRootViewCoordinates();
        field.value = input->value().get();
    }
    if (loginFormsAreEqual(m_focusedLoginForm, form))
        return;
    m_focusedLoginForm = WTF::move(form);
    send(Messages::WebPageProxy::FocusedLoginFormChanged(m_focusedLoginForm));
}

void WebPage::loginFormFieldDidChange(HTMLInputElement&)
{
    // New values make a new sign in.
    m_lastSubmittedLoginForm = std::nullopt;
    updateFocusedLoginForm();
}

void WebPage::loginFormWillBeSubmitted(HTMLFormElement& formElement)
{
    if (!m_loginFormAutofillEnabled)
        return;

    // The first password field of the form, from which AutofillElements finds the others, as for a focused field.
    RefPtr<HTMLInputElement> password;
    for (auto& listedElement : formElement.copyListedElementsVector()) {
        RefPtr input = dynamicDowncast<HTMLInputElement>(listedElement->asHTMLElement());
        if (input && input->isPasswordField()) {
            password = WTF::move(input);
            break;
        }
    }
    if (!password)
        return;
    auto form = loginFormForInput(*password, false);
    if (!form)
        return;
    if (m_lastSubmittedLoginFormElement.get() == &formElement && loginFormsAreEqual(m_lastSubmittedLoginForm, form))
        return;
    m_lastSubmittedLoginFormElement = &formElement;
    m_lastSubmittedLoginForm = form;
    send(Messages::WebPageProxy::LoginFormSubmitted(*form));
}

void WebPage::autofillLoginForm(const ElementContext& focusedField, const Vector<std::pair<ElementContext, String>>& values)
{
    // Only while the field that the password manager answered for has the focus, in the same document, and only the
    // fields of its form.
    RefPtr input = dynamicDowncast<HTMLInputElement>(elementForContext(focusedField));
    if (!m_loginFormAutofillEnabled || !input || input != m_focusedLoginFormInput.get())
        return;
    auto elements = AutofillElements::computeAutofillElements(*input);
    if (!elements)
        return;
    std::array<RefPtr<HTMLInputElement>, 3> fields { elements->usernameField(), elements->passwordField(), elements->secondPasswordField() };
    for (auto& [context, value] : values) {
        RefPtr field = dynamicDowncast<HTMLInputElement>(elementForContext(context));
        if (!field || &field->document() != &input->document() || std::ranges::find(fields, field) == fields.end())
            continue;
        // Marked before the value changes, as the input listeners of the page may check, like a field filled through
        // the form manager of a web process extension.
        if (!value.isEmpty())
            field->setAutofilled(true);
        field->setValueForUser(value);
    }
    updateFocusedLoginForm();
}

} // namespace WebKit
