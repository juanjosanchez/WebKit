/*
 * Copyright (C) 2017-2020 Apple Inc. All rights reserved.
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

#include <WebCore/HTMLInputElement.h>
#include <wtf/TZoneMalloc.h>

namespace WebCore {

// Whether a password field that the page turned into a text field, as to show the password, still counts as one.
enum class IncludeShownPasswordFields : bool { No, Yes };

class AutofillElements {
    WTF_MAKE_TZONE_ALLOCATED(AutofillElements);
public:
    WEBCORE_EXPORT static std::optional<AutofillElements> computeAutofillElements(Ref<HTMLInputElement>, IncludeShownPasswordFields = IncludeShownPasswordFields::No);
    WEBCORE_EXPORT static bool isPasswordField(const HTMLInputElement&, IncludeShownPasswordFields);
    WEBCORE_EXPORT void autofill(String, String);

    const HTMLInputElement* username() const { return m_username.get(); }
    RefPtr<HTMLInputElement> usernameField() const { return m_username; }
    RefPtr<HTMLInputElement> passwordField() const { return m_password; }
    RefPtr<HTMLInputElement> secondPasswordField() const { return m_secondPassword; }
private:
    AutofillElements(RefPtr<HTMLInputElement>&&, RefPtr<HTMLInputElement>&&, RefPtr<HTMLInputElement>&&);
    const RefPtr<HTMLInputElement> m_username;
    const RefPtr<HTMLInputElement> m_password;
    const RefPtr<HTMLInputElement> m_secondPassword;
};

} // namespace WebCore
