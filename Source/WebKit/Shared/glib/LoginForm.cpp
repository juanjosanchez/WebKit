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

#include "config.h"
#include "LoginForm.h"

namespace WebKit {

static bool fieldsAreEqual(const LoginFormField& a, const LoginFormField& b)
{
    return a.role == b.role
        && a.element.isSameElement(b.element)
        && a.element.boundingRect == b.element.boundingRect
        && a.value == b.value
        && a.name == b.name
        && a.idAttribute == b.idAttribute
        && a.type == b.type
        && a.autocomplete == b.autocomplete
        && a.placeholder == b.placeholder
        && a.label == b.label;
}

bool loginFormsAreEqual(const std::optional<LoginForm>& a, const std::optional<LoginForm>& b)
{
    if (!a || !b)
        return !a && !b;
    if (a->frameID != b->frameID || a->focusedField != b->focusedField || a->fields.size() != b->fields.size())
        return false;
    for (size_t i = 0; i < a->fields.size(); ++i) {
        if (!fieldsAreEqual(a->fields[i], b->fields[i]))
            return false;
    }
    return true;
}

} // namespace WebKit
