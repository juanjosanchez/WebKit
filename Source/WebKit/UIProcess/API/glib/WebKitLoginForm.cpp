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
#include "WebKitLoginForm.h"

#include "WebKitLoginFormPrivate.h"
#include "WebKitSecurityOriginPrivate.h"
#include <WebCore/FloatRect.h>
#include <wtf/text/CString.h>

using namespace WebKit;

/**
 * WebKitLoginForm: (ref-func webkit_login_form_ref) (unref-func webkit_login_form_unref)
 *
 * The user name and password fields of a form.
 *
 * A #WebKitLoginForm tells a password manager about the fields of a login form, as WebKit finds them: the user name
 * field and the password field next to it, or the two password fields of a form to sign up. It is a snapshot: the
 * #WebKitWebView gives a new one when the fields change.
 *
 * WebKit only finds login forms while #WebKitSettings:enable-login-form-autofill is enabled, so that the values
 * that users type stay in the web process unless the application asks for them.
 *
 * #WebKitWebView:focused-login-form is the login form whose field has the focus, which a password manager can fill
 * with webkit_web_view_autofill_login_form(). #WebKitWebView::login-form-submitted tells of the values of a login
 * form that the page submits, for the password manager to offer to save them.
 *
 * The security origin is the one of the document that the frame of the form committed, as the UI process knows it,
 * and only forms of http and https documents are given.
 *
 * Since: 2.56
 */

struct LoginFormFieldStrings {
    UTF8CString value;
    UTF8CString name;
    UTF8CString idAttribute;
    UTF8CString type;
    UTF8CString autocomplete;
    UTF8CString placeholder;
    UTF8CString label;
};

struct _WebKitLoginForm {
    _WebKitLoginForm(const LoginForm& form, const WebCore::SecurityOriginData& origin)
        : form(form)
        , securityOrigin(webkitSecurityOriginCreate(WebCore::SecurityOriginData { origin }))
    {
        strings = WTF::map(form.fields, [](auto& field) {
            return LoginFormFieldStrings { field.value.utf8(), field.name.utf8(), field.idAttribute.utf8(), field.type.utf8(), field.autocomplete.utf8(), field.placeholder.utf8(), field.label.utf8() };
        });
    }

    ~_WebKitLoginForm()
    {
        webkit_security_origin_unref(securityOrigin);
    }

    LoginForm form;
    WebKitSecurityOrigin* securityOrigin;
    Vector<LoginFormFieldStrings> strings;
    int referenceCount { 1 };
};

G_DEFINE_BOXED_TYPE(WebKitLoginForm, webkit_login_form, webkit_login_form_ref, webkit_login_form_unref)

WebKitLoginForm* webkitLoginFormCreate(const LoginForm& loginForm, const WebCore::SecurityOriginData& origin)
{
    WebKitLoginForm* form = static_cast<WebKitLoginForm*>(fastMalloc(sizeof(WebKitLoginForm)));
    new (form) WebKitLoginForm(loginForm, origin);
    return form;
}

const LoginForm& webkitLoginFormGetLoginForm(WebKitLoginForm* form)
{
    return form->form;
}

/**
 * webkit_login_form_ref:
 * @form: a #WebKitLoginForm
 *
 * Atomically increments the reference count of @form by one.
 *
 * Returns: The passed #WebKitLoginForm
 *
 * Since: 2.56
 */
WebKitLoginForm* webkit_login_form_ref(WebKitLoginForm* form)
{
    g_return_val_if_fail(form, nullptr);

    g_atomic_int_inc(&form->referenceCount);
    return form;
}

/**
 * webkit_login_form_unref:
 * @form: a #WebKitLoginForm
 *
 * Atomically decrements the reference count of @form by one.
 *
 * If the reference count drops to 0, all memory allocated by #WebKitLoginForm is released.
 *
 * Since: 2.56
 */
void webkit_login_form_unref(WebKitLoginForm* form)
{
    g_return_if_fail(form);

    if (g_atomic_int_dec_and_test(&form->referenceCount)) {
        form->~WebKitLoginForm();
        fastFree(form);
    }
}

/**
 * webkit_login_form_get_security_origin:
 * @form: a #WebKitLoginForm
 *
 * Gets the security origin of the document of @form, which a password manager uses to find the logins it can fill.
 *
 * Returns: (transfer none): a #WebKitSecurityOrigin
 *
 * Since: 2.56
 */
WebKitSecurityOrigin* webkit_login_form_get_security_origin(WebKitLoginForm* form)
{
    g_return_val_if_fail(form, nullptr);

    return form->securityOrigin;
}

/**
 * webkit_login_form_get_n_fields:
 * @form: a #WebKitLoginForm
 *
 * Gets the number of fields of @form, from one to three.
 *
 * Returns: the number of fields
 *
 * Since: 2.56
 */
guint webkit_login_form_get_n_fields(WebKitLoginForm* form)
{
    g_return_val_if_fail(form, 0);

    return form->form.fields.size();
}

/**
 * webkit_login_form_get_focused_field:
 * @form: a #WebKitLoginForm
 *
 * Gets the index of the field of @form that has the focus.
 *
 * Returns: the index of the focused field, or -1 if none has the focus
 *
 * Since: 2.56
 */
gint webkit_login_form_get_focused_field(WebKitLoginForm* form)
{
    g_return_val_if_fail(form, -1);

    return form->form.focusedField ? static_cast<gint>(*form->form.focusedField) : -1;
}

/**
 * webkit_login_form_get_field_role:
 * @form: a #WebKitLoginForm
 * @index: the index of a field
 *
 * Gets what the field at @index is for.
 *
 * Returns: a #WebKitLoginFormFieldRole
 *
 * Since: 2.56
 */
WebKitLoginFormFieldRole webkit_login_form_get_field_role(WebKitLoginForm* form, guint index)
{
    g_return_val_if_fail(form, WEBKIT_LOGIN_FORM_FIELD_ROLE_USERNAME);
    g_return_val_if_fail(index < form->form.fields.size(), WEBKIT_LOGIN_FORM_FIELD_ROLE_USERNAME);

    switch (form->form.fields[index].role) {
    case LoginFormField::Role::Username:
        return WEBKIT_LOGIN_FORM_FIELD_ROLE_USERNAME;
    case LoginFormField::Role::Password:
        return WEBKIT_LOGIN_FORM_FIELD_ROLE_PASSWORD;
    case LoginFormField::Role::NewPassword:
        return WEBKIT_LOGIN_FORM_FIELD_ROLE_NEW_PASSWORD;
    }
    RELEASE_ASSERT_NOT_REACHED();
}

/**
 * webkit_login_form_get_field_id:
 * @form: a #WebKitLoginForm
 * @index: the index of a field
 *
 * Gets an identifier of the element of the field at @index, which stays the same in every #WebKitLoginForm of that
 * element while the element lives. Identifiers of elements in different web processes can be the same.
 *
 * Returns: the identifier of the field
 *
 * Since: 2.56
 */
guint64 webkit_login_form_get_field_id(WebKitLoginForm* form, guint index)
{
    g_return_val_if_fail(form, 0);
    g_return_val_if_fail(index < form->form.fields.size(), 0);

    auto& nodeIdentifier = form->form.fields[index].element.nodeIdentifier;
    return nodeIdentifier ? nodeIdentifier->toUInt64() : 0;
}

/**
 * webkit_login_form_get_field_value:
 * @form: a #WebKitLoginForm
 * @index: the index of a field
 *
 * Gets the value of the field at @index.
 *
 * Returns: the value of the field
 *
 * Since: 2.56
 */
const gchar* webkit_login_form_get_field_value(WebKitLoginForm* form, guint index)
{
    g_return_val_if_fail(form, nullptr);
    g_return_val_if_fail(index < form->form.fields.size(), nullptr);

    return form->strings[index].value.legacyCStringPointer();
}

/**
 * webkit_login_form_get_field_attribute:
 * @form: a #WebKitLoginForm
 * @index: the index of a field
 * @name: the name of an attribute: "name", "id", "type", "autocomplete" or "placeholder"
 *
 * Gets an attribute of the element of the field at @index, which password managers use to tell login forms apart.
 * The type is the one the element has, which is "text" for a missing or unknown type attribute.
 *
 * Returns: (nullable): the value of the attribute, empty if the element has none, or %NULL for another name
 *
 * Since: 2.56
 */
const gchar* webkit_login_form_get_field_attribute(WebKitLoginForm* form, guint index, const gchar* name)
{
    g_return_val_if_fail(form, nullptr);
    g_return_val_if_fail(index < form->form.fields.size(), nullptr);
    g_return_val_if_fail(name, nullptr);

    auto& strings = form->strings[index];
    if (!g_strcmp0(name, "name"))
        return strings.name.legacyCStringPointer();
    if (!g_strcmp0(name, "id"))
        return strings.idAttribute.legacyCStringPointer();
    if (!g_strcmp0(name, "type"))
        return strings.type.legacyCStringPointer();
    if (!g_strcmp0(name, "autocomplete"))
        return strings.autocomplete.legacyCStringPointer();
    if (!g_strcmp0(name, "placeholder"))
        return strings.placeholder.legacyCStringPointer();
    return nullptr;
}

/**
 * webkit_login_form_get_field_label:
 * @form: a #WebKitLoginForm
 * @index: the index of a field
 *
 * Gets the text of the first label of the field at @index.
 *
 * Returns: the text of the label, empty if the field has none
 *
 * Since: 2.56
 */
const gchar* webkit_login_form_get_field_label(WebKitLoginForm* form, guint index)
{
    g_return_val_if_fail(form, nullptr);
    g_return_val_if_fail(index < form->form.fields.size(), nullptr);

    return form->strings[index].label.legacyCStringPointer();
}

/**
 * webkit_login_form_get_field_bounds:
 * @form: a #WebKitLoginForm
 * @index: the index of a field
 * @x: (out) (optional): return location for the left of the field
 * @y: (out) (optional): return location for the top of the field
 * @width: (out) (optional): return location for the width of the field
 * @height: (out) (optional): return location for the height of the field
 *
 * Gets where the field at @index is, in the coordinates of the view.
 *
 * Since: 2.56
 */
void webkit_login_form_get_field_bounds(WebKitLoginForm* form, guint index, gdouble* x, gdouble* y, gdouble* width, gdouble* height)
{
    g_return_if_fail(form);
    g_return_if_fail(index < form->form.fields.size());

    auto& rect = form->form.fields[index].element.boundingRect;
    if (x)
        *x = rect.x();
    if (y)
        *y = rect.y();
    if (width)
        *width = rect.width();
    if (height)
        *height = rect.height();
}
