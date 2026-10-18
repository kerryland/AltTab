from datetime import datetime
from pathlib import Path
import VersionHelper


def create_version_header():
    """Create version.h file from template using version info."""
    version_info = VersionHelper.read_version_info()

    # Read template
    version_h_template = Path(__file__).parent.resolve() / 'version.h.template'
    with open(version_h_template, 'r', encoding='utf-8') as f:
        template = f.read()

    # Replace placeholders (customize placeholders as per your template)
    output = template.replace('{MAJOR}', version_info[0]) \
                    .replace('{MINOR}', version_info[1]) \
                    .replace('{PATCH}', version_info[2]) \
                    .replace('{YEAR}', str(datetime.now().year))

    # Write version.h
    version_h_output = Path(__file__).parent.parent.resolve() / "source/version.h"
    with open(version_h_output, 'w', encoding='utf-8') as f:
        f.write(output)
    print(f"Generated {version_h_output} with version {'.'.join(version_info)}")
    print("Version header file created successfully.")


if __name__ == "__main__":
    create_version_header()