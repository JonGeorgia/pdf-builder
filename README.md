[English](README.md) | [Русский](README.ru.md)

# PDFuse

**PDFuse** is a free program for creating PDFs from Markdown documents. It produces PDF documents that are equally convenient for humans and for use by AI models (including RAG systems — *Retrieval-Augmented Generation*).

👤 **For humans** — a beautifully typeset document: title page, fonts, logos, tables, images, mathematical formulas, and more.

🤖 **For AI** — a machine-readable data source. PDFs created by PDFuse contain all source data: Markdown texts, CSS styles, and images. Markdown is the most convenient format for chunking and indexing while preserving document structure.

---

## Features

- **Markdown → PDF.** Build PDFs from ready-made Markdown files with full control over layout via CSS.
- **PDF → Markdown.** Restore sources from a PDF created by PDFuse, or extract the text layer from any PDF that has an accessible text layer.
- **Full CSS support** via WeasyPrint: fonts, colors, gradients, `@page`, headers and footers, page counters, multi-page layouts.
- **Partial LaTeX formula support** via the `math2svg` filter (matplotlib): fractions, roots, integrals, sums, limits, Greek letters. Multi-line environments (`\begin{cases}`, `\begin{pmatrix}`, `\begin{align}`) are not supported — such formulas can be inserted as images.
- **Batch processing.** All `.md` files in a folder are converted in one pass with a single style.
- **Embedded sources.** PDFuse embeds the Markdown, CSS, and images into the PDF — for later extraction or use in RAG systems.
- **Graphic monolith.** Optionally, you can get a PDF without a text layer or attachments.
- **Multilingual interface:** Russian, English, German, French.

---

## Installation

### Linux (AppImage)

1. Download `PDFuse-x86_64.AppImage` from the [Releases](https://github.com/JonGeorgia/pdf-builder/releases) page.
2. Make the file executable:
   ```bash
   chmod +x PDFuse-x86_64.AppImage
   ```
3. Run it:
   ```bash
   ./PDFuse-x86_64.AppImage
   ```

No dependencies need to be installed — everything is inside one file.

### Windows

**In development.** Stay tuned in the [Releases](https://github.com/JonGeorgia/pdf-builder/releases) section.

---

## Usage

### "Markdown → PDF" mode

The program processes a **folder** containing:

- one or more `.md` files — source texts;
- a `style/` subfolder with a `style.css` file — describes the PDF layout, logo images, and other CSS elements;
- an `images/` subfolder — images to insert into the generated PDFs (optional).

Example structure:

```
My book/
├── Chapter 1.md
├── Chapter 2.md
├── style/
│   ├── style.css
│   ├── logo.png
│   └── fonts/
│       └── MyFont-Bold.ttf
└── images/
    ├── 1.jpg
    └── 2.png
```

For each `.md` file, a `.pdf` file with the same name is created in the same folder.

### "PDF → Markdown" mode

- If the PDF was created by PDFuse and contains the `SOURCES.bin` attachment, the program restores the full structure: `.md` files, `style/style.css`, `images/`.
- If the PDF was created by another program, only the text layer is extracted (OCR — text recognition from images — is not performed).

---

## Use in RAG systems

When generating a PDF, the program embeds the source files (Markdown texts, CSS styles, images) as a single block — the `SOURCES.bin` attachment.

This means:

- the text in the PDF remains machine-readable and is easily extracted by standard parsers;
- at any time, the source Markdown can be retrieved — the most convenient format for chunking while preserving document structure;
- humans read the PDF while AI works with the sources — without quality loss on either side.

**How to extract the sources:**

### 1. Via PDFuse

Open the PDF in "PDF → Markdown" mode.

### 2. Manually

Extract the `SOURCES.bin` attachment with any PDF tool (Adobe Acrobat, PDF-XChange, `mutool extract`) and unpack it with a suitable tool.
For an unpacking example, see the "Python" section below.

### 3. Python

- Structure of `SOURCES.bin`

```text
["PDFUSE_SOURCES" + 3 null bytes] → [zlib stream] → [ZIP with files]
        17-byte header              compressed data   .md, style/, images/
```

- Extracting `SOURCES.bin` from a PDF with pypdf

```python
from pypdf import PdfReader
reader = PdfReader("document.pdf")
# PDF attachments are in reader.attachments: {name: [bytes, ...]}
bin_data = None
for name, blobs in reader.attachments.items():
    for blob in blobs:
        if name == "SOURCES.bin" or blob.startswith(b"PDFUSE_SOURCES"):
            bin_data = blob
            break
    if bin_data is not None:
        break
if bin_data is None:
    raise FileNotFoundError("No SOURCES.bin attachment in the PDF")
with open("SOURCES.bin", "wb") as f:
    f.write(bin_data)
```

- Extracting the ZIP from `SOURCES.bin`

  Via Python:

  ```python
  import zlib

  with open("SOURCES.bin", "rb") as f:
      data = f.read()

  zip_bytes = zlib.decompress(data[17:])  # strip the 17-byte header

  with open("sources.zip", "wb") as f:
      f.write(zip_bytes)
  ```

  Or with a single terminal command:

  ```bash
  python3 -c "
  import zlib
  data = open('SOURCES.bin','rb').read()
  open('sources.zip','wb').write(zlib.decompress(data[17:]))
  "
  unzip sources.zip
  ```

- Extracting data from the ZIP

  ```python
  import zipfile

  with zipfile.ZipFile("sources.zip") as zf:
      zf.extractall("sources/")
  ```

After that, the `sources/` folder will contain files roughly like this:

```text
sources/
├── document.md
├── style/
│   └── style.css
└── images/
    └── *.png
```

- RAG example

```python
from pathlib import Path
from langchain_text_splitters import RecursiveCharacterTextSplitter

# ... your vector store / embedder here

sources_dir = extract_sources_from_pdf("document.pdf", "sources")

# Find the Markdown file inside sources/.
md_files = list(Path(sources_dir).rglob("*.md"))
assert md_files, "No Markdown found inside the archive"
text = md_files[0].read_text(encoding="utf-8")
splitter = RecursiveCharacterTextSplitter(chunk_size=800, chunk_overlap=100)
chunks = splitter.split_text(text)

# Then as usual: chunks → embeddings → vector store.
# Example:
# vs.add_texts(chunks, metadatas=[{"source": str(md_files[0])}] * len(chunks))
print(f"Done: {len(chunks)} chunks from {md_files[0]}")
```

Images from `sources/images/` can be indexed separately for multimodal search: index them together with the text and store the file path in the chunk metadata.

- Summary

1. For example, use `pypdf` to extract `SOURCES.bin` from the PDF.
2. Strip the 17-byte header and call `zlib.decompress` to get the ZIP.
3. Extract the archive with `zipfile.extractall`.
4. For RAG, take the Markdown file from the archive, split it into chunks, and send them to the vector index.

---

## Building from source

### Requirements

- Ubuntu 22.04 or newer (built on 24.04).
- Packages: `build-essential pkg-config libgtk-3-dev zlib1g-dev libzip-dev libmupdf-dev`.

### Build

```bash
git clone https://github.com/JonGeorgia/pdf-builder.git
cd pdf-builder
make
```

After building, the `pdf-builder-c` binary appears. It uses:

- `pandoc` — for Markdown → HTML conversion;
- `weasyprint` — for HTML → PDF conversion;
- `Python 3` with `matplotlib` — for rendering LaTeX formulas (see `math2svg.py`).

These dependencies are not included in the project's system packages. It is recommended to use a **Python virtual environment**:

```bash
python3 -m venv venv
venv/bin/pip install -r requirements.txt
```

The list of required Python packages is in [`requirements.txt`](requirements.txt) (it lists `matplotlib` and `weasyprint`). After installation, activate the environment and run the program:

```bash
source venv/bin/activate
./pdf-builder-c
```

---

## Third-party libraries and licenses

PDFuse is distributed under the **GNU GPL v3** license (see the [LICENSE](LICENSE) file).

The program uses the following third-party libraries. Their license texts are in the [`licenses/`](licenses/) folder:

| Library | License | Purpose |
|---|---|---|
| [MuPDF](https://mupdf.com/) | AGPL v3 | PDF processing |
| [pandoc](https://pandoc.org/) | GPL v2+ | Markdown → HTML |
| [WeasyPrint](https://weasyprint.org/) | BSD 3-Clause | HTML → PDF |
| [GTK 3](https://www.gtk.org/) | LGPL v2.1 | Graphical interface |
| [Python](https://www.python.org/) | PSF License 2 | Runtime for WeasyPrint |

The source code of these libraries is available at the links above.

---

## Feedback

- **Bugs, questions, suggestions:** [GitHub Issues](https://github.com/JonGeorgia/pdf-builder/issues).
- **Support the project:**
  - [Boosty](https://boosty.to/automatonlab)
  - [Ko-fi](https://ko-fi.com/automatonlab)

---

## License

Copyright (C) AutomatonLab (JonGeorgia).

The program is distributed under the **GNU General Public License v3.0 or later**. The full text is in the [LICENSE](LICENSE) file.
