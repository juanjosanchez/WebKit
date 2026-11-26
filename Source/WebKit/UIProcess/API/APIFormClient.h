/*
 * Copyright (C) 2014 Apple Inc. All rights reserved.
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

#include <wtf/CompletionHandler.h>
#include <wtf/TZoneMallocInlines.h>

#if PLATFORM(GTK) || PLATFORM(WPE)
#include "LoginForm.h"
#include <WebCore/SecurityOriginData.h>
#endif

namespace WebKit {
struct FrameInfoData;
class WebFrameProxy;
class WebPageProxy;
}

namespace API {
class Object;

class FormClient {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(FormClient);
public:
    virtual ~FormClient() { }

    virtual void willSubmitForm(WebKit::WebPageProxy&, WebKit::WebFrameProxy&, WebKit::WebFrameProxy&, WebKit::FrameInfoData&&, WebKit::FrameInfoData&&, const Vector<std::pair<WTF::String, WTF::String>>&, API::Object*, const WTF::URL&, const WTF::String&, CompletionHandler<void()>&& completionHandler)
    {
        completionHandler();
    }

#if PLATFORM(GTK) || PLATFORM(WPE)
    // The origin is the one of the frame of the form, as the UI process knows it, and only http and https ones.
    virtual void focusedLoginFormChanged(WebKit::WebPageProxy&, const std::optional<WebKit::LoginForm>&, const std::optional<WebCore::SecurityOriginData>&) { }
    virtual void loginFormSubmitted(WebKit::WebPageProxy&, const WebKit::LoginForm&, const WebCore::SecurityOriginData&) { }
#endif
};

} // namespace API
