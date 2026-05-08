from fastapi import FastAPI, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from starlette.middleware.wsgi import WSGIMiddleware

from .services.cli import CliError
from .api import accounts, positions, strategies, prices, import_, reports, analyze, portfolio
from .frontend import create_dash_app

# Single FastAPI app
app = FastAPI(title="IBKR Options Analyzer API", version="0.1.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


@app.exception_handler(CliError)
async def cli_error_handler(request: Request, exc: CliError):
    return JSONResponse(
        status_code=502,
        content={"error": str(exc), "detail": exc.stderr},
    )


# Register API routers with /api prefix
app.include_router(accounts.router, prefix="/api/accounts", tags=["accounts"])
app.include_router(positions.router, prefix="/api/positions", tags=["positions"])
app.include_router(strategies.router, prefix="/api/strategies", tags=["strategies"])
app.include_router(prices.router, prefix="/api/prices", tags=["prices"])
app.include_router(import_.router, prefix="/api/import", tags=["import"])
app.include_router(reports.router, prefix="/api/reports", tags=["reports"])
app.include_router(analyze.router, prefix="/api/analyze", tags=["analyze"])
app.include_router(portfolio.router, prefix="/api/portfolio", tags=["portfolio"])


@app.get("/api/health")
def health_check():
    return {"status": "ok"}


# Mount Dash frontend — must be last so /api/* routes take priority
dash_app = create_dash_app()
app.mount("/", WSGIMiddleware(dash_app.server))
