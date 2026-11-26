/*
 * Copyright (C) 2026 Igalia S.L.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"

#include "TestMain.h"
#include "WebKitTestServer.h"
#include "WebViewTest.h"
#include <wtf/glib/GUniquePtr.h>

static WebKitTestServer* kServer;

static const char* kLoginFormHTML =
    "<!doctype html><html><body>"
    "<script>var inputs = 0; addEventListener('input', () => inputs++, true);</script>"
    "<form action='/welcome' method='get'>"
    "<input name='user' autocomplete='username'>"
    "<input name='pass' type='password' autocomplete='current-password'>"
    "<button>Sign in</button></form></body></html>";

static void serverCallback(SoupServer*, SoupServerMessage* message, const char* path, GHashTable*, gpointer)
{
    if (soup_server_message_get_method(message) != SOUP_METHOD_GET) {
        soup_server_message_set_status(message, SOUP_STATUS_NOT_IMPLEMENTED, nullptr);
        return;
    }

    const char* body = g_str_equal(path, "/login") ? kLoginFormHTML : "<!doctype html><title>Welcome</title>";
    soup_server_message_set_status(message, SOUP_STATUS_OK, nullptr);
    soup_message_body_append(soup_server_message_get_response_body(message), SOUP_MEMORY_STATIC, body, strlen(body));
    soup_message_body_complete(soup_server_message_get_response_body(message));
}

class LoginFormTest : public WebViewTest {
public:
    MAKE_GLIB_TEST_FIXTURE(LoginFormTest);

    LoginFormTest()
    {
        showInWindow();
        webkit_settings_set_enable_login_form_autofill(webkit_web_view_get_settings(m_webView.get()), TRUE);
        g_signal_connect(m_webView.get(), "notify::focused-login-form", G_CALLBACK(focusedLoginFormChanged), this);
        g_signal_connect(m_webView.get(), "login-form-submitted", G_CALLBACK(loginFormSubmitted), this);
    }

    ~LoginFormTest()
    {
        g_signal_handlers_disconnect_matched(m_webView.get(), G_SIGNAL_MATCH_DATA, 0, 0, nullptr, nullptr, this);
        g_clear_pointer(&m_submittedForm, webkit_login_form_unref);
    }

    static void focusedLoginFormChanged(WebKitWebView*, GParamSpec*, LoginFormTest* test)
    {
        if (test->m_waitingForFocusedLoginForm)
            g_main_loop_quit(test->m_mainLoop);
    }

    static void loginFormSubmitted(WebKitWebView*, WebKitLoginForm* form, LoginFormTest* test)
    {
        test->m_submittedCount++;
        g_clear_pointer(&test->m_submittedForm, webkit_login_form_unref);
        test->m_submittedForm = webkit_login_form_ref(form);
        if (test->m_waitingForSubmittedForm)
            g_main_loop_quit(test->m_mainLoop);
    }

    void loadLoginForm()
    {
        loadURI(kServer->getURIForPath("/login"));
        waitUntilLoadFinished();
    }

    WebKitLoginForm* focusedLoginForm() const { return webkit_web_view_get_focused_login_form(m_webView.get()); }

    // Waits until the web view has a focused login form whose focused field is the one at index, or none if index
    // is -1.
    WebKitLoginForm* waitForFocusedField(int index)
    {
        auto matches = [this, index] {
            auto* form = focusedLoginForm();
            return index < 0 ? !form : form && webkit_login_form_get_focused_field(form) == index;
        };
        m_waitingForFocusedLoginForm = true;
        while (!matches())
            g_main_loop_run(m_mainLoop);
        m_waitingForFocusedLoginForm = false;
        return focusedLoginForm();
    }

    // WebKit tells of the focused login form before it answers a script run after the change.
    void waitForPendingLoginFormUpdates()
    {
        runJavaScriptAndWaitUntilFinished("void 0", nullptr);
    }

    WebKitLoginForm* waitForSubmittedForm()
    {
        m_waitingForSubmittedForm = true;
        while (!m_submittedForm)
            g_main_loop_run(m_mainLoop);
        m_waitingForSubmittedForm = false;
        return m_submittedForm;
    }

    GUniquePtr<char> stringResult(const char* script)
    {
        return GUniquePtr<char>(WebViewTest::javascriptResultToCString(runJavaScriptAndWaitUntilFinished(script, nullptr)));
    }

    // As the user types it, with the input event that the page hears.
    void type(const char* name, const char* value)
    {
        GUniquePtr<char> script(g_strdup_printf("(field => { field.value = '%s'; field.dispatchEvent(new Event('input', { bubbles: true })); })"
            "(document.querySelector('[name=%s]'))", value, name));
        runJavaScriptAndWaitUntilFinished(script.get(), nullptr);
    }

    // Fills the focused login form with a user name and a password.
    void fill(const char* user, const char* password)
    {
        auto* form = focusedLoginForm();
        g_assert_nonnull(form);
        g_assert_cmpuint(webkit_login_form_get_n_fields(form), ==, 2);
        const char* values[] = { user, password };
        webkit_web_view_autofill_login_form(m_webView.get(), form, values, 2);
    }

#if ENABLE(WPE_PLATFORM)
    bool hasWPEPlatform()
    {
        if (m_display)
            return true;
        g_test_skip("Needs the WPE platform API");
        return false;
    }

    // The view loses or gets the focus, as when another window takes it, while the field keeps the focus of its document.
    void setViewFocused(bool focused)
    {
        auto* view = webkit_web_view_get_wpe_view(m_webView.get());
        if (focused)
            wpe_view_focus_in(view);
        else
            wpe_view_focus_out(view);
        assertJavaScriptBecomesTrue(focused ? "document.hasFocus()" : "!document.hasFocus()");
        // WebKit handles a blur in the next run loop iteration.
        waitForPendingLoginFormUpdates();
    }
#endif

    bool m_waitingForFocusedLoginForm { false };
    bool m_waitingForSubmittedForm { false };
    unsigned m_submittedCount { 0 };
    WebKitLoginForm* m_submittedForm { nullptr };
};

static void testLoginFormDisabled(LoginFormTest* test, gconstpointer)
{
    webkit_settings_set_enable_login_form_autofill(webkit_web_view_get_settings(test->webView()), FALSE);
    test->loadLoginForm();
    test->runJavaScriptAndWaitUntilFinished("document.forms[0].user.focus()", nullptr);
    test->waitForPendingLoginFormUpdates();
    g_assert_null(test->focusedLoginForm());

    test->type("user", "typed-user");
    test->type("pass", "typed-secret");
    test->runJavaScriptAndWaitUntilFinished("document.forms[0].requestSubmit()", nullptr);
    test->waitUntilLoadFinished();
    g_assert_cmpuint(test->m_submittedCount, ==, 0);
}

static void testLoginFormFocusedForm(LoginFormTest* test, gconstpointer)
{
    test->loadLoginForm();
    test->runJavaScriptAndWaitUntilFinished("document.forms[0].user.focus()", nullptr);
    auto* form = test->waitForFocusedField(0);
    g_assert_cmpuint(webkit_login_form_get_n_fields(form), ==, 2);
    g_assert_cmpint(webkit_login_form_get_field_role(form, 0), ==, WEBKIT_LOGIN_FORM_FIELD_ROLE_USERNAME);
    g_assert_cmpint(webkit_login_form_get_field_role(form, 1), ==, WEBKIT_LOGIN_FORM_FIELD_ROLE_PASSWORD);
    g_assert_cmpstr(webkit_login_form_get_field_attribute(form, 0, "name"), ==, "user");
    g_assert_cmpstr(webkit_login_form_get_field_attribute(form, 1, "type"), ==, "password");
    g_assert_cmpstr(webkit_login_form_get_field_value(form, 0), ==, "");

    auto* origin = webkit_login_form_get_security_origin(form);
    g_assert_nonnull(origin);
    g_assert_cmpstr(webkit_security_origin_get_protocol(origin), ==, "http");
    ASSERT_CMP_CSTRING(webkit_security_origin_get_host(origin), ==, kServer->baseURL().host().toString().utf8());
    g_assert_cmpuint(webkit_security_origin_get_port(origin), ==, kServer->port());

    test->runJavaScriptAndWaitUntilFinished("document.forms[0].pass.focus()", nullptr);
    test->waitForFocusedField(1);

    test->runJavaScriptAndWaitUntilFinished("document.forms[0].pass.blur()", nullptr);
    test->waitForFocusedField(-1);
}

static void testLoginFormAutofill(LoginFormTest* test, gconstpointer)
{
    test->loadLoginForm();
    test->runJavaScriptAndWaitUntilFinished("document.forms[0].user.focus()", nullptr);
    test->waitForFocusedField(0);

    test->fill("filled-user", "filled-secret");
    test->assertJavaScriptBecomesTrue("document.forms[0].pass.value === 'filled-secret'");
    auto user = test->stringResult("document.forms[0].user.value");
    g_assert_cmpstr(user.get(), ==, "filled-user");
    g_assert_true(WebViewTest::javascriptResultToBoolean(test->runJavaScriptAndWaitUntilFinished("document.forms[0].user.matches(':-webkit-autofill')", nullptr)));
    g_assert_true(WebViewTest::javascriptResultToBoolean(test->runJavaScriptAndWaitUntilFinished("inputs >= 2", nullptr)));
}

static void testLoginFormAutofillAfterBlur(LoginFormTest* test, gconstpointer)
{
    test->loadLoginForm();
    test->runJavaScriptAndWaitUntilFinished("document.forms[0].user.focus()", nullptr);
    test->waitForFocusedField(0);
    auto* form = webkit_login_form_ref(test->focusedLoginForm());

    // The field lost the focus after the password manager got the form, so the fill must not land.
    test->runJavaScriptAndWaitUntilFinished("document.forms[0].user.blur()", nullptr);
    test->waitForFocusedField(-1);
    const char* values[] = { "filled-user", "filled-secret" };
    webkit_web_view_autofill_login_form(test->webView(), form, values, 2);
    webkit_login_form_unref(form);
    test->waitForPendingLoginFormUpdates();
    auto user = test->stringResult("document.forms[0].user.value");
    g_assert_cmpstr(user.get(), ==, "");
    auto password = test->stringResult("document.forms[0].pass.value");
    g_assert_cmpstr(password.get(), ==, "");
}

static void testLoginFormSubmitted(LoginFormTest* test, gconstpointer)
{
    test->loadLoginForm();
    test->runJavaScriptAndWaitUntilFinished("document.forms[0].user.focus()", nullptr);
    test->waitForFocusedField(0);
    test->type("user", "typed-user");
    test->type("pass", "typed-secret");
    test->runJavaScriptAndWaitUntilFinished("document.forms[0].requestSubmit()", nullptr);

    auto* form = test->waitForSubmittedForm();
    g_assert_cmpuint(webkit_login_form_get_n_fields(form), ==, 2);
    g_assert_cmpstr(webkit_login_form_get_field_value(form, 0), ==, "typed-user");
    g_assert_cmpstr(webkit_login_form_get_field_value(form, 1), ==, "typed-secret");
    test->waitUntilLoadFinished();
    g_assert_cmpuint(test->m_submittedCount, ==, 1);
}

// Many sign in pages have a button that shows the password, which turns its field into a text field.
static void testLoginFormShownPassword(LoginFormTest* test, gconstpointer)
{
    test->loadLoginForm();
    test->runJavaScriptAndWaitUntilFinished("document.forms[0].pass.focus()", nullptr);
    test->waitForFocusedField(1);

    test->runJavaScriptAndWaitUntilFinished("document.forms[0].pass.type = 'text'", nullptr);
    test->waitForPendingLoginFormUpdates();
    auto* form = test->focusedLoginForm();
    g_assert_nonnull(form);
    g_assert_cmpint(webkit_login_form_get_focused_field(form), ==, 1);
    g_assert_cmpint(webkit_login_form_get_field_role(form, 1), ==, WEBKIT_LOGIN_FORM_FIELD_ROLE_PASSWORD);
    g_assert_cmpstr(webkit_login_form_get_field_attribute(form, 1, "type"), ==, "text");

    // The user name field before a shown password still makes a login form with it, and both can be filled.
    test->runJavaScriptAndWaitUntilFinished("document.forms[0].user.focus()", nullptr);
    form = test->waitForFocusedField(0);
    g_assert_cmpuint(webkit_login_form_get_n_fields(form), ==, 2);
    g_assert_cmpint(webkit_login_form_get_field_role(form, 1), ==, WEBKIT_LOGIN_FORM_FIELD_ROLE_PASSWORD);
    test->fill("filled-user", "filled-secret");
    test->assertJavaScriptBecomesTrue("document.forms[0].pass.value === 'filled-secret'");

    test->type("user", "typed-user");
    test->type("pass", "typed-secret");
    test->runJavaScriptAndWaitUntilFinished("document.forms[0].requestSubmit()", nullptr);
    auto* submitted = test->waitForSubmittedForm();
    g_assert_cmpint(webkit_login_form_get_field_role(submitted, 1), ==, WEBKIT_LOGIN_FORM_FIELD_ROLE_PASSWORD);
    g_assert_cmpstr(webkit_login_form_get_field_value(submitted, 1), ==, "typed-secret");
}

#if ENABLE(WPE_PLATFORM)
// A password manager may show its list of logins, or ask to unlock them, in a window of its own, and fill then.
static void testLoginFormViewFocusOut(LoginFormTest* test, gconstpointer)
{
    if (!test->hasWPEPlatform())
        return;

    test->loadLoginForm();
    test->setViewFocused(true);
    test->runJavaScriptAndWaitUntilFinished("document.forms[0].user.focus()", nullptr);
    test->waitForFocusedField(0);

    test->setViewFocused(false);
    g_assert_nonnull(test->focusedLoginForm());
    test->fill("filled-user", "filled-secret");
    test->assertJavaScriptBecomesTrue("document.forms[0].pass.value === 'filled-secret'");

    test->setViewFocused(true);
    g_assert_nonnull(test->focusedLoginForm());
    g_assert_cmpint(webkit_login_form_get_focused_field(test->focusedLoginForm()), ==, 0);
}

// A field removed while the view does not have the focus reports no blur.
static void testLoginFormFieldRemovedWhileViewUnfocused(LoginFormTest* test, gconstpointer)
{
    if (!test->hasWPEPlatform())
        return;

    test->loadLoginForm();
    test->setViewFocused(true);
    test->runJavaScriptAndWaitUntilFinished("document.forms[0].user.focus()", nullptr);
    test->waitForFocusedField(0);

    test->setViewFocused(false);
    test->runJavaScriptAndWaitUntilFinished("document.forms[0].remove()", nullptr);
    test->setViewFocused(true);
    test->waitForFocusedField(-1);
}

// A script that blurs the field while the view does not have the focus takes its form away at once.
static void testLoginFormFieldBlurredWhileViewUnfocused(LoginFormTest* test, gconstpointer)
{
    if (!test->hasWPEPlatform())
        return;

    test->loadLoginForm();
    test->setViewFocused(true);
    test->runJavaScriptAndWaitUntilFinished("document.forms[0].user.focus()", nullptr);
    test->waitForFocusedField(0);

    test->setViewFocused(false);
    test->runJavaScriptAndWaitUntilFinished("document.forms[0].user.blur()", nullptr);
    test->waitForFocusedField(-1);
}
#endif

void beforeAll()
{
    kServer = new WebKitTestServer();
    kServer->run(serverCallback);

    LoginFormTest::add("WebKitLoginForm", "disabled", testLoginFormDisabled);
    LoginFormTest::add("WebKitLoginForm", "focused-form", testLoginFormFocusedForm);
    LoginFormTest::add("WebKitLoginForm", "autofill", testLoginFormAutofill);
    LoginFormTest::add("WebKitLoginForm", "autofill-after-blur", testLoginFormAutofillAfterBlur);
    LoginFormTest::add("WebKitLoginForm", "submitted", testLoginFormSubmitted);
    LoginFormTest::add("WebKitLoginForm", "shown-password", testLoginFormShownPassword);
#if ENABLE(WPE_PLATFORM)
    LoginFormTest::add("WebKitLoginForm", "view-focus-out", testLoginFormViewFocusOut);
    LoginFormTest::add("WebKitLoginForm", "field-removed-while-view-unfocused", testLoginFormFieldRemovedWhileViewUnfocused);
    LoginFormTest::add("WebKitLoginForm", "field-blurred-while-view-unfocused", testLoginFormFieldBlurredWhileViewUnfocused);
#endif
}

void afterAll()
{
    delete kServer;
}
