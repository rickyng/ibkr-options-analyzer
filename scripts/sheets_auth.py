#!/usr/bin/env python3
"""
Google Sheets OAuth authentication helper.

Handles OAuth 2.0 flow for Google Sheets API access.
Tokens stored in ~/.ibkr-options-analyzer/google/

Usage:
    python scripts/sheets_auth.py login    # Start OAuth flow (opens browser)
    python scripts/sheets_auth.py status   # Check authentication status
    python scripts/sheets_auth.py logout   # Clear stored tokens
"""

import argparse
import json
import os
import sys
from pathlib import Path

from google.auth.transport.requests import Request
from google.oauth2.credentials import Credentials
from google_auth_oauthlib.flow import InstalledAppFlow

SCOPES = ['https://www.googleapis.com/auth/spreadsheets']

def get_config_dir() -> Path:
    """Get the config directory for storing credentials."""
    config_dir = Path.home() / ".ibkr-options-analyzer" / "google"
    config_dir.mkdir(parents=True, exist_ok=True)
    return config_dir

def get_credentials_path() -> Path:
    """Path to OAuth client credentials JSON (user must provide)."""
    return get_config_dir() / "credentials.json"

def get_token_path() -> Path:
    """Path to stored OAuth tokens."""
    return get_config_dir() / "token.json"

def get_credentials() -> Credentials | None:
    """
    Load credentials from storage or initiate OAuth flow.

    Returns valid credentials or None if authentication failed.
    """
    creds = None
    token_path = get_token_path()
    creds_path = get_credentials_path()

    # Load existing token if available
    if token_path.exists():
        creds = Credentials.from_authorized_user_file(str(token_path), SCOPES)

    # If credentials are invalid or expired, refresh or re-auth
    if not creds or not creds.valid:
        if creds and creds.expired and creds.refresh_token:
            try:
                creds.refresh(Request())
                # Save refreshed credentials
                with open(token_path, 'w') as f:
                    f.write(creds.to_json())
                print("Token refreshed successfully.")
            except Exception as e:
                print(f"Failed to refresh token: {e}")
                creds = None
        else:
            # Need to do full OAuth flow
            if not creds_path.exists():
                print(f"Error: Credentials file not found at {creds_path}")
                print("Please download OAuth credentials from Google Cloud Console:")
                print("  1. Go to https://console.cloud.google.com/apis/credentials")
                print("  2. Create an OAuth 2.0 Client ID (Desktop app)")
                print("  3. Download JSON and save to:", creds_path)
                return None

            try:
                flow = InstalledAppFlow.from_client_secrets_file(
                    str(creds_path), SCOPES
                )
                creds = flow.run_local_server(port=0)

                # Save credentials for future use
                with open(token_path, 'w') as f:
                    f.write(creds.to_json())
                print("Authentication successful. Token saved.")
            except Exception as e:
                print(f"OAuth flow failed: {e}")
                return None

    return creds

def login():
    """Initiate OAuth login flow."""
    print("Starting Google OAuth authentication...")
    creds = get_credentials()
    if creds:
        print("Successfully authenticated!")
        print(f"Token stored at: {get_token_path()}")
        return 0
    return 1

def status():
    """Check authentication status."""
    token_path = get_token_path()
    creds_path = get_credentials_path()

    if not creds_path.exists():
        print("Not configured: credentials.json missing")
        print(f"  Expected at: {creds_path}")
        return 1

    if not token_path.exists():
        print("Not authenticated: token.json missing")
        print("  Run: python scripts/sheets_auth.py login")
        return 1

    try:
        creds = Credentials.from_authorized_user_file(str(token_path), SCOPES)
        if creds.valid:
            print("Authenticated: token is valid")
            return 0
        elif creds.expired and creds.refresh_token:
            print("Token expired but can be refreshed")
            print("  Will auto-refresh on next use")
            return 0
        else:
            print("Token is invalid")
            print("  Run: python scripts/sheets_auth.py login")
            return 1
    except Exception as e:
        print(f"Error reading token: {e}")
        return 1

def logout():
    """Clear stored tokens."""
    token_path = get_token_path()
    if token_path.exists():
        token_path.unlink()
        print("Token deleted.")
    else:
        print("No token to delete.")
    return 0

def main():
    parser = argparse.ArgumentParser(description="Google Sheets OAuth helper")
    parser.add_argument('command', choices=['login', 'status', 'logout'],
                        help='Action to perform')

    args = parser.parse_args()

    if args.command == 'login':
        return login()
    elif args.command == 'status':
        return status()
    elif args.command == 'logout':
        return logout()

if __name__ == '__main__':
    sys.exit(main())