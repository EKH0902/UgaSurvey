import json
import re
import sys
from html.parser import HTMLParser
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SURVEY_FILENAME = re.compile(r"^\d{3,}\.html$")


class SurveyMetadataParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.in_title = False
        self.in_survey_data = False
        self.title_parts = []
        self.data_parts = []

    def handle_starttag(self, tag, attrs):
        attributes = dict(attrs)
        if tag == "title":
            self.in_title = True
        if tag == "script" and attributes.get("id") == "survey-data":
            self.in_survey_data = True

    def handle_endtag(self, tag):
        if tag == "title":
            self.in_title = False
        if tag == "script":
            self.in_survey_data = False

    def handle_data(self, data):
        if self.in_title:
            self.title_parts.append(data)
        if self.in_survey_data:
            self.data_parts.append(data)


def describe_survey(path):
    parser = SurveyMetadataParser()
    parser.feed(path.read_text(encoding="utf-8"))
    title = " ".join(" ".join(parser.title_parts).split())
    description = ""

    if parser.data_parts:
        try:
            payload = json.loads("".join(parser.data_parts))
            survey = payload["surveys"][0]
            title = str(survey.get("title") or title).strip()
            description = str(survey.get("description") or "").strip()
        except (json.JSONDecodeError, KeyError, IndexError, TypeError, AttributeError) as error:
            print(f"Warning: could not read embedded survey data from {path}: {error}", file=sys.stderr)

    return {
        "file": path.name,
        "title": title or path.stem,
        "description": description,
    }


def build_manifest(surveys_dir):
    files = sorted(
        (path for path in surveys_dir.glob("*.html") if SURVEY_FILENAME.fullmatch(path.name)),
        key=lambda path: int(path.stem),
    )
    return {
        "version": 1,
        "surveys": [describe_survey(path) for path in files],
    }


def main():
    surveys_dir = ROOT / "docs" / "surveys"
    surveys_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = surveys_dir / "index.json"
    manifest_path.write_text(
        json.dumps(build_manifest(surveys_dir), ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"Generated {manifest_path} from numeric HTML survey files.")


if __name__ == "__main__":
    main()
