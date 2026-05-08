"""Dash application factory."""

from dash import Dash
import dash_bootstrap_components as dbc

from .layout import create_layout
from .callbacks import register_callbacks


def create_dash_app() -> Dash:
    """Create and configure the Dash application."""
    app = Dash(
        __name__,
        external_stylesheets=[dbc.themes.DARKLY],
        requests_pathname_prefix="/",
        suppress_callback_exceptions=True,
        meta_tags=[
            {"name": "viewport", "content": "width=device-width, initial-scale=1"},
        ],
    )
    app.layout = create_layout()
    register_callbacks(app)
    return app
