/*
 * Copyright (C) 2026 Igalia S.L.
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

#pragma once

#include <WebCore/ElementContext.h>
#include <WebCore/FrameIdentifier.h>
#include <optional>
#include <wtf/Vector.h>
#include <wtf/text/WTFString.h>

namespace WebKit {

// A field of a login form, as AutofillElements finds it, with what a password manager needs to know of it.
struct LoginFormField {
    enum class Role : uint8_t {
        Username,
        Password,
        NewPassword,
    };

    Role role { Role::Username };
    // The element, and its bounds in root view coordinates.
    WebCore::ElementContext element;
    String value;
    String name;
    String idAttribute;
    String type;
    String autocomplete;
    String placeholder;
    String label;
};

// The user name and password fields of a form in a frame, for a password manager to fill, or to save after a sign in.
struct LoginForm {
    WebCore::FrameIdentifier frameID;
    Vector<LoginFormField> fields;
    // The index in fields of the field that has the focus, if one has.
    std::optional<uint32_t> focusedField;
};

bool loginFormsAreEqual(const std::optional<LoginForm>&, const std::optional<LoginForm>&);

} // namespace WebKit
