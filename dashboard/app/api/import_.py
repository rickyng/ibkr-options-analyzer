from pathlib import Path

from fastapi import APIRouter, HTTPException, UploadFile, File

from ..config import settings
from ..services.cli import download_flex, import_csv, CliError

router = APIRouter()


@router.post("/flex-download")
def trigger_download(token: str, query_id: str, account: str, force: bool = False):
    try:
        result = download_flex(token, query_id, account, force)
    except CliError as e:
        raise HTTPException(status_code=400, detail=str(e))
    return {"data": result if result else {"status": "downloaded", "account": account}}


@router.post("/discover")
def discover_and_import():
    try:
        result = import_csv()
    except CliError as e:
        raise HTTPException(status_code=400, detail=str(e))
    return {"data": result if result else {"status": "imported"}}


@router.post("/upload")
async def upload_csv(file: UploadFile = File(...)):
    content = await file.read()
    tmp_path = Path(settings.db_path).parent / "uploads" / file.filename
    tmp_path.parent.mkdir(parents=True, exist_ok=True)
    tmp_path.write_bytes(content)
    try:
        result = import_csv(tmp_path)
    except CliError as e:
        raise HTTPException(status_code=400, detail=str(e))
    return {"data": result if result else {"status": "imported", "filename": file.filename}}
