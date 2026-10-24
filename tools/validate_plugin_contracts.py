"""Validate SDK schemas and shipped manifests. Developer check: pip install jsonschema."""
import json
from pathlib import Path
from jsonschema import Draft202012Validator

root = Path(__file__).resolve().parents[1]
manifest_schema = json.loads((root / "docs/plugins/manifest.schema.json").read_text(encoding="utf-8"))
api_schema = json.loads((root / "docs/plugins/api.schema.json").read_text(encoding="utf-8"))
Draft202012Validator.check_schema(manifest_schema)
Draft202012Validator.check_schema(api_schema)
validator = Draft202012Validator(manifest_schema)
for path in [*root.glob("plugins/*/plugin.json"), *root.glob("sdk/examples/*/plugin.json")]:
    validator.validate(json.loads(path.read_text(encoding="utf-8")))
    print(f"Valid manifest: {path.relative_to(root)}")
api = Draft202012Validator(api_schema)
api.validate({"method": "activate", "locale": "en", "settings": {}, "params": {"dataDirectory": "C:/test"}})
api.validate({"games": [{"appId": 620, "name": "Portal 2", "available": True}]})
api.validate({"page": {"title": "Translations", "sections": [{"title": "All", "items": [
    {"title": "Portal 2", "uri": "https://example.org/gra/portal_2", "gameKey": "steam:620"}
]}]}})
api.validate({"drawer": {"title": "Translations", "sections": [{"title": "All", "items": []}]}})
api.validate({"method": "details", "locale": "pl", "settings": {}, "params": {"playtimeSeconds": None}})
api.validate({"comparison": {"gameKey": "steam:620", "mainSeconds": 28800, "extraSeconds": 43200, "completionSeconds": 72000}})
assert not api.is_valid({"comparison": {"gameKey": "steam:620", "mainSeconds": -1}})
assert not validator.is_valid({"apiVersion": 1, "architecture": "windows-x64", "id": "example.bad",
                              "version": "1.0.0", "entry": "bad.dll", "name": "Bad", "contributions": [
                                  {"id": "test", "kind": "context", "title": "Test", "command": "test", "activeGame": True}]})
controls = {"drawer": {"title": "Service", "sections": [{"title": "Status", "items": [
    {"title": "Load", "command": "load", "enabled": False}, {"title": "Status", "subtitle": "Unloaded"}
]}]}}
api.validate(controls)
controls["drawer"]["sections"][0]["items"][0]["uri"] = "https://example.org"
assert not api.is_valid(controls)
assert not validator.is_valid({"apiVersion": 1, "architecture": "windows-x64", "id": "example.bad",
                               "version": "1.0.0", "entry": "bad.dll", "name": "Bad", "tags": ["widget", "widget"]})
search_page = {"page": {"title": "Catalog", "back": "back", "refresh": 1,
    "search": {"command": "find", "placeholder": "Search", "query": "portal"}, "sections": []}}
api.validate(search_page)
download = {"id":"1", "title":"Example", "phase":"converting", "status":"Unpacking",
            "destination":"C:/Downloads/example.zip", "fraction":0.5, "command":"job.1"}
api.validate({"downloads":[download]})
thumbnail_page = {"page":{"title":"Catalog","sections":[{"title":"Downloads","items":[{"title":"Game","command":"job.1","downloadId":"1"}]}]}}
api.validate(thumbnail_page)
thumbnail_page["page"]["sections"][0]["items"][0]["gameKey"] = "steam:620"
assert not api.is_valid(thumbnail_page)
api.validate({"downloads":[dict(download, phase="verifying")]})
api.validate({"downloads":[dict(download, phase="paused")]})
api.validate({"revealFile":"C:/Downloads/example.zip"})
api.validate({"queued":{"count":1,"title":"Example"}})
api.validate({"method":"downloads","locale":"pl","settings":{},"params":{"command":"browse"}})
api.validate({"page":{"title":"Download extras?","dialog":True,"back":"extras.skip","sections":[]}})
assert not api.is_valid({"downloads":[dict(download, fraction=1.1)]})
assert not api.is_valid({"queued":{"count":0,"title":"Example"}})
search_page["page"]["refresh"] = 0
assert not api.is_valid(search_page)
assert not api.is_valid({"page": {"title": "X", "back": "two words", "sections": []}})
api.validate({"method": "command", "locale": "pl", "settings": {}, "params": {"command": "find", "query": "portal", "refresh": False}})
assert not api.is_valid({"method": "unsupported", "locale": "en", "settings": {}, "params": {}})
assert not api.is_valid({"games": [{"appId": -1, "name": "Wrong", "available": True}]})
print("SDK manifest/request/response schemas validated")
