/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
 * Portions Copyright (c) 2010 Motorola Mobility, Inc. All rights reserved.
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
#include "WebPageProxy.h"

#include "APIFormClient.h"
#include "LoginForm.h"
#include "PageClient.h"
#include "WebFrameProxy.h"
#include "WebKitWebResourceLoadManager.h"
#include "WebPageMessages.h"
#include "WebPageProxyInternals.h"
#include "WebPreferences.h"
#include "WebProcessProxy.h"
#include <WebCore/NotImplemented.h>
#include <WebCore/SearchPopupMenu.h>
#include <WebCore/SecurityOriginData.h>
#include <WebCore/UserAgent.h>

namespace WebKit {

String WebPageProxy::userAgentForURL(const URL& url)
{
    if (url.isNull() || !preferences().needsSiteSpecificQuirks())
        return this->userAgent();

    auto userAgent = WebCore::standardUserAgentForURL(url);
    return userAgent.isNull() ? this->userAgent() : userAgent;
}

String WebPageProxy::standardUserAgent(const String& applicationNameForUserAgent)
{
    return WebCore::standardUserAgent(applicationNameForUserAgent);
}

void WebPageProxy::saveRecentSearches(IPC::Connection&, const String&, const Vector<WebCore::RecentSearch>&)
{
    notImplemented();
}

void WebPageProxy::loadRecentSearches(IPC::Connection&, const String&, CompletionHandler<void(Vector<WebCore::RecentSearch>&&)>&& completionHandler)
{
    notImplemented();
    completionHandler({ });
}

void WebPageProxy::didInitiateLoadForResource(WebCore::ResourceLoaderIdentifier resourceID, WebCore::FrameIdentifier frameID, WebCore::ResourceRequest&& request)
{
    if (auto* manager = pageClient() ? pageClient()->webResourceLoadManager() : nullptr)
        manager->didInitiateLoad(resourceID, frameID, WTF::move(request));
}

void WebPageProxy::didSendRequestForResource(WebCore::ResourceLoaderIdentifier resourceID, WebCore::FrameIdentifier frameID, WebCore::ResourceRequest&& request, WebCore::ResourceResponse&& redirectResponse)
{
    if (auto* manager = pageClient() ? pageClient()->webResourceLoadManager() : nullptr)
        manager->didSendRequest(resourceID, frameID, WTF::move(request), WTF::move(redirectResponse));
}

void WebPageProxy::didReceiveResponseForResource(WebCore::ResourceLoaderIdentifier resourceID, WebCore::FrameIdentifier frameID, WebCore::ResourceResponse&& response)
{
    if (auto* manager = pageClient() ? pageClient()->webResourceLoadManager() : nullptr)
        manager->didReceiveResponse(resourceID, frameID, WTF::move(response));
}

void WebPageProxy::didFinishLoadForResource(WebCore::ResourceLoaderIdentifier resourceID, WebCore::FrameIdentifier frameID, WebCore::ResourceError&& error)
{
    if (auto* manager = pageClient() ? pageClient()->webResourceLoadManager() : nullptr)
        manager->didFinishLoad(resourceID, frameID, WTF::move(error));
}

void WebPageProxy::scheduleActivityStateUpdate()
{
    if (internals().activityStateChangeTimer.isActive())
        return;

    internals().activityStateChangeTimer.startOneShot(0_s);
}

WebKitWebView* WebPageProxy::platformView()
{
    return internals().platformView.get();
}

void WebPageProxy::setPlatformView(WebKitWebView *view)
{
    internals().platformView = GWeakPtr(view);
}

// AutofillElements finds one to three fields, and the focused field is one of them.
static bool isValidLoginForm(const LoginForm& form)
{
    return !form.fields.isEmpty() && form.fields.size() <= 3 && (!form.focusedField || *form.focusedField < form.fields.size());
}

// The origin of the frame of a login form, from what the UI process knows of the frame, never from the message, so
// that a web process cannot make a password manager fill or save the logins of another site. Only http and https.
static std::optional<WebCore::SecurityOriginData> originOfLoginForm(const WebFrameProxy& frame)
{
    const URL& url = frame.url();
    if (!url.protocolIsInHTTPFamily())
        return std::nullopt;
    return WebCore::SecurityOriginData::fromURL(url);
}

void WebPageProxy::focusedLoginFormChanged(IPC::Connection& connection, std::optional<LoginForm>&& form)
{
    std::optional<WebCore::SecurityOriginData> origin;
    if (form) {
        MESSAGE_CHECK_BASE(isValidLoginForm(*form), connection);
        // The frame may have gone meanwhile, or moved to another process.
        RefPtr frame = WebFrameProxy::webFrame(form->frameID);
        if (frame && frame->page() == this && &frame->process() == WebProcessProxy::fromConnection(connection).ptr())
            origin = originOfLoginForm(*frame);
        if (!origin)
            form = std::nullopt;
    }
    m_formClient->focusedLoginFormChanged(*this, form, origin);
}

void WebPageProxy::loginFormSubmitted(IPC::Connection& connection, LoginForm&& form)
{
    MESSAGE_CHECK_BASE(isValidLoginForm(form), connection);
    RefPtr frame = WebFrameProxy::webFrame(form.frameID);
    if (!frame || frame->page() != this || &frame->process() != WebProcessProxy::fromConnection(connection).ptr())
        return;
    if (auto origin = originOfLoginForm(*frame))
        m_formClient->loginFormSubmitted(*this, form, *origin);
}

// The values are those of the fields of the form, in order, a null one for a field to leave as it is.
void WebPageProxy::autofillLoginForm(const LoginForm& form, const Vector<String>& values)
{
    if (!form.focusedField || *form.focusedField >= form.fields.size())
        return;
    Vector<std::pair<WebCore::ElementContext, String>> fieldValues;
    for (size_t i = 0; i < values.size() && i < form.fields.size(); ++i) {
        if (!values[i].isNull())
            fieldValues.append({ form.fields[i].element, values[i] });
    }
    if (fieldValues.isEmpty())
        return;
    sendToProcessContainingFrame(form.frameID, Messages::WebPage::AutofillLoginForm(form.fields[*form.focusedField].element, WTF::move(fieldValues)));
}

} // namespace WebKit
