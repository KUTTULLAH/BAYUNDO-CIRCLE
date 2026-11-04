import json
import sys
import time
import urllib.error
import urllib.request


BASE_URL = "http://127.0.0.1:8080/api/v1"
ADMIN_PHONE = "+254712345678"
ADMIN_PASSWORD = "ci-only-bootstrap-password"
MEMBER_PASSWORD = "ci-only-member-password"
TARGET = {"amount": "5000.00", "currency": "KES"}


def request(method, path, body=None, token=None, headers=None):
    request_headers = dict(headers or {})
    data = None
    if body is not None:
        request_headers["Content-Type"] = "application/json"
        data = json.dumps(body).encode()
    if token:
        request_headers["Authorization"] = f"Bearer {token}"
    req = urllib.request.Request(
        f"{BASE_URL}{path}",
        data=data,
        headers=request_headers,
        method=method,
    )
    try:
        response = urllib.request.urlopen(req, timeout=5)
    except urllib.error.HTTPError as error:
        response = error
    payload = response.read()
    return response.status, json.loads(payload) if payload else None, response.headers


def expect(actual, expected, description):
    if actual != expected:
        raise AssertionError(f"{description}: expected {expected}, got {actual}")


def wait_for_api():
    for _ in range(60):
        try:
            status, body, _ = request("GET", "/health")
            if status == 200 and body == {"status": "ok"}:
                return
        except (OSError, urllib.error.URLError):
            pass
        time.sleep(1)
    raise AssertionError("API did not become ready.")


def accept_invitation(token):
    return request(
        "POST",
        "/auth/accept-invite",
        {"token": token, "password": MEMBER_PASSWORD},
    )


def login(phone, password):
    return request("POST", "/auth/login", {"phone": phone, "password": password})


def create_account(admin_token, phone, full_name, role="member"):
    status, body, _ = request(
        "POST",
        "/admin/members",
        {
            "fullName": full_name,
            "phone": phone,
            "monthlyTarget": TARGET,
            "role": role,
        },
        admin_token,
    )
    expect(status, 202, f"Create {role} account")
    if not isinstance(body, dict) or not isinstance(body.get("member"), dict):
        raise AssertionError("Create-account response must contain the member object.")
    member = body["member"]
    expect(member["status"], "active", "New member record status")
    expect(member["accountStatus"], "invited", "New account status before activation")
    token = body.get("invitationToken")
    if not isinstance(token, str) or not token:
        raise AssertionError("Create-account response must return its invitation token.")
    return member, token


def main():
    wait_for_api()

    status, _, _ = request(
        "POST",
        "/admin/members",
        {
            "fullName": "Unauthorized Member",
            "phone": "+254700000001",
            "monthlyTarget": TARGET,
        },
    )
    expect(status, 401, "Unauthenticated member creation")

    status, admin_login, _ = login(ADMIN_PHONE, ADMIN_PASSWORD)
    expect(status, 200, "Bootstrap admin login")
    first_admin_token = admin_login["accessToken"]

    second_admin, second_admin_invite = create_account(
        first_admin_token, "+254700000002", "Second Administrator", "admin"
    )
    expect(login("+254700000002", MEMBER_PASSWORD)[0], 401, "Admin login before invite")
    expect(accept_invitation(second_admin_invite)[0], 204, "Activate second admin")
    expect(accept_invitation(second_admin_invite)[0], 409, "Reject reused admin invitation")

    status, second_admin_login, _ = login("+254700000002", MEMBER_PASSWORD)
    expect(status, 200, "Second admin login")
    expect(second_admin_login["member"]["role"], "admin", "Second admin role")
    second_admin_token = second_admin_login["accessToken"]

    member, invitation_token = create_account(
        first_admin_token, "+254700000003", "Immediate Activation Member"
    )
    member_id = member["id"]
    expect(login("+254700000003", MEMBER_PASSWORD)[0], 401, "Member login before invite")

    status, roster, _ = request(
        "GET", "/admin/members?status=active", token=first_admin_token
    )
    expect(status, 200, "List active members")
    roster_member = next((item for item in roster["items"] if item["id"] == member_id), None)
    if roster_member is None:
        raise AssertionError("The newly created active member is missing from the roster.")
    expect(roster_member["paymentStatus"], "unpaid", "New member payment status")

    expect(accept_invitation(invitation_token)[0], 204, "Activate member account")
    expect(accept_invitation(invitation_token)[0], 409, "Reject reused member invitation")
    status, member_login, _ = login("+254700000003", MEMBER_PASSWORD)
    expect(status, 200, "Member login after invite")
    expect(member_login["member"]["status"], "active", "Activated member status")

    expect(request("POST", "/auth/logout", token=second_admin_token)[0], 204, "Logout")
    expect(
        request("GET", "/admin/members", token=second_admin_token)[0],
        401,
        "Revoked session",
    )
    status, _, _ = request(
        "POST",
        "/admin/members",
        {
            "fullName": "Duplicate Phone",
            "phone": "+254700000003",
            "monthlyTarget": TARGET,
        },
        first_admin_token,
    )
    expect(status, 409, "Reject duplicate phone")

    preflight_status, _, preflight_headers = request(
        "OPTIONS",
        "/auth/login",
        headers={
            "Origin": "http://localhost:3000",
            "Access-Control-Request-Method": "POST",
            "Access-Control-Request-Headers": "authorization,content-type",
        },
    )
    expect(preflight_status, 204, "Local dashboard CORS preflight")
    expect(
        preflight_headers.get("Access-Control-Allow-Origin"),
        "http://localhost:3000",
        "Allowed local CORS origin",
    )
    _, _, denied_headers = request(
        "OPTIONS",
        "/auth/login",
        headers={
            "Origin": "https://untrusted.example",
            "Access-Control-Request-Method": "POST",
            "Access-Control-Request-Headers": "authorization,content-type",
        },
    )
    if denied_headers.get("Access-Control-Allow-Origin"):
        raise AssertionError("The API allowed an untrusted browser origin.")

    print("Immediate member activation lifecycle checks passed.")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, KeyError, TypeError, ValueError) as error:
        print(f"Member lifecycle CI test failed: {error}", file=sys.stderr)
        sys.exit(1)
