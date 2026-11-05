import json
import os
import sys
import time
import urllib.error
import urllib.request


BASE_URL = "http://127.0.0.1:8080/api/v1"
BOOTSTRAP_TOKEN = os.environ["BOOTSTRAP_TOKEN"]
ADMIN_PASSWORD = "ci-only-bootstrap-password"


def request(method, path, body=None, bootstrap_token=None, headers=None):
    request_headers = dict(headers or {})
    data = None
    if bootstrap_token is not None:
        request_headers["X-Bootstrap-Token"] = bootstrap_token
    if body is not None:
        request_headers["Content-Type"] = "application/json"
        data = json.dumps(body).encode()
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


def main():
    if len(BOOTSTRAP_TOKEN) < 32:
        raise AssertionError("CI bootstrap token must contain at least 32 characters.")

    wait_for_api()
    payload = {
        "chamaName": "Jirani Circle CI",
        "fullName": "CI Administrator",
        "phone": "+254712345678",
        "password": ADMIN_PASSWORD,
    }

    expect(
        request("POST", "/admin/bootstrap", payload)[0],
        401,
        "Bootstrap without token",
    )
    expect(
        request(
            "POST",
            "/admin/bootstrap",
            payload,
            bootstrap_token="wrong-bootstrap-token",
        )[0],
        401,
        "Bootstrap with incorrect token",
    )

    status, body, _ = request(
        "POST",
        "/admin/bootstrap",
        payload,
        bootstrap_token=BOOTSTRAP_TOKEN,
    )
    expect(status, 201, "Initial administrator bootstrap")
    if not isinstance(body, dict) or not isinstance(body.get("admin"), dict):
        raise AssertionError("Bootstrap response must contain the admin object.")
    if body["admin"].get("role") != "admin":
        raise AssertionError("Bootstrap must return the created administrator.")

    expect(
        request(
            "POST",
            "/admin/bootstrap",
            {
                **payload,
                "chamaName": "Second Chama",
                "phone": "+254712345679",
            },
            bootstrap_token=BOOTSTRAP_TOKEN,
        )[0],
        409,
        "Reject second administrator bootstrap",
    )

    preflight_status, _, preflight_headers = request(
        "OPTIONS",
        "/admin/bootstrap",
        headers={
            "Origin": "http://localhost:3000",
            "Access-Control-Request-Method": "POST",
            "Access-Control-Request-Headers": "content-type,x-bootstrap-token",
        },
    )
    expect(preflight_status, 204, "Bootstrap CORS preflight")
    if "X-Bootstrap-Token" not in preflight_headers.get(
        "Access-Control-Allow-Headers", ""
    ):
        raise AssertionError("Bootstrap token header is not allowed by CORS.")

    print("One-time API bootstrap checks passed.")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, KeyError, TypeError, ValueError) as error:
        print(f"Bootstrap API CI test failed: {error}", file=sys.stderr)
        sys.exit(1)
