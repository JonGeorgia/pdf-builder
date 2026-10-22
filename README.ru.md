[English](README.md) | [Русский](README.ru.md)

# PDFuse

**PDFuse** — бесплатная программа для создания PDF из документов с разметкой Markdown. PDFuse создаёт PDF-документы, одинаково удобные и для человека, и для использования ИИ моделями (в том числе в RAG-системах (*Retrieval-Augmented Generation*)).

👤 **Для человека** — это красиво свёрстанный документ: с титульной страницей, шрифтами, логотипами, таблицами, изображениями, математическими формулами и прочим.

🤖 **Для ИИ** — это машиночитаемый источник данных, так как создаваемые в программе PDF содержат все исходные данные: Markdown-тексты, CSS-стиль и изображения. А данные в разметке Markdown — самый удобный формат для разбиения на чанки и индексации с сохранением структуры документа.

---

## Возможности

- **Markdown → PDF.** Сборка PDF из готовых Markdown-файлов с полным контролем оформления через CSS.
- **PDF → Markdown.** Восстановление исходников из PDF, созданного в PDFuse, или извлечение текстового слоя из любого PDF если в нём есть доступный текстовый слой.
- **Полная поддержка CSS** через движок WeasyPrint: шрифты, цвета, градиенты, `@page`, колонтитулы, счётчики страниц, многостраничная вёрстка.
- **Частичная поддержка LaTeX-формул** через фильтр `math2svg` (matplotlib): дроби, корни, интегралы, суммы, пределы, греческие буквы. Не поддерживаются: многострочные окружения (`\begin{cases}`, `\begin{pmatrix}`, `\begin{align}`) — их можно вставить как изображение.
- **Пакетная обработка.** Все `.md`-файлы в папке конвертируются за один проход с единым стилем.
- **Встраивание исходников.** PDFuse вкладывает в PDF сам Markdown, CSS и изображения — для последующего извлечения или использования в RAG-системах.
- **Графический монолит.** Опционально можно получить PDF без текстового слоя и вложений.
- **Многоязычный интерфейс:** русский, английский, немецкий, французский.

---

## Установка

### Linux (AppImage)

1. Скачайте файл `PDFuse-x86_64.AppImage` со страницы [Releases](https://github.com/JonGeorgia/pdf-builder/releases).
2. Сделайте файл исполняемым:
   ```bash
   chmod +x PDFuse-x86_64.AppImage
   ```
3. Запустите:
   ```bash
   ./PDFuse-x86_64.AppImage
   ```

Установка зависимостей не требуется — всё внутри одного файла.

### Windows

**В разработке.** Следите за обновлениями в разделе [Releases](https://github.com/JonGeorgia/pdf-builder/releases).

---

## Использование

### Режим «Markdown → PDF»

Программа обрабатывает **папку**, в которой лежат:

- один или несколько файлов `.md` — исходные тексты;
- подпапка `style/` с файлом `style.css` — описывает оформление PDF, изображения для логотипов, и прочие элементы для стилей CSS;
- подпапка `images/` — изображения, которые необходимо вставить в создаваемые PDF (необязательно).

Пример структуры:

```
Моя книга/
├── Глава 1.md
├── Глава 2.md
├── style/
│   ├── style.css
│   ├── logo.png
│   └── fonts/
│       └── MyFont-Bold.ttf
└── images/
    ├── 1.jpg
    └── 2.png
```

Для каждого `.md` в той же папке создаётся одноимённый `.pdf`.

### Режим «PDF → Markdown»

- Если документ PDF создан в PDFuse и содержит вложение `SOURCES.bin`, программа восстановит полную структуру: `.md`-файлы, `style/style.css`, `images/`.
- Если PDF создан другой программой, извлекается только текстовый слой (при этом распознавание текста из графической информации (OCR) не выполняется).

---

## Использование в RAG-системах

При генерации PDF программа вкладывает в него исходные файлы (Markdown-тексты, CSS-стиль, изображения) как единый блок — вложение `SOURCES.bin`.

Благодаря этому:

- текст в PDF остаётся машиночитаемым и легко извлекается стандартными парсерами;
- в любой момент можно достать исходный Markdown — самый удобный формат для разбиения на чанки с сохранением структуры;
- человек читает PDF, а ИИ работает с исходниками — без потери качества.

**Как извлечь исходники:**

### 1. Через PDFuse

Откройте PDF в режиме «PDF → Markdown».

### 2. Вручную

Извлеките вложение `SOURCES.bin` любым PDF-инструментом (Adobe Acrobat, PDF-XChange, `mutool extract`), и распакуйте подходящим инструментом.
Пример распаковки — см. раздел «Python».

### 3. Python

- Структура `SOURCES.bin`

```text
["PDFUSE_SOURCES" + 3 нулевых байта] → [zlib-поток] → [ZIP с файлами]
              17 байт заголовка       сжатые данные    .md, style/, images/
```

- Извлечение `SOURCES.bin` из PDF при помощи pypdf

```python
from pypdf import PdfReader
reader = PdfReader("document.pdf")
# Вложения PDF лежат в reader.attachments: {имя: [байты, ...]}
bin_data = None
for name, blobs in reader.attachments.items():
    for blob in blobs:
        if name == "SOURCES.bin" or blob.startswith(b"PDFUSE_SOURCES"):
            bin_data = blob
            break
    if bin_data is not None:
        break
if bin_data is None:
    raise FileNotFoundError("В PDF нет вложения SOURCES.bin")
with open("SOURCES.bin", "wb") as f:
    f.write(bin_data)
```

- Извлечение ZIP из `SOURCES.bin`

  Через Python:

```python
    import zlib
    with open("SOURCES.bin", "rb") as f:
        data = f.read()
    zip_bytes = zlib.decompress(data[17:])  # отрезаем 17-байтовый заголовок
    with open("sources.zip", "wb") as f:
        f.write(zip_bytes)
```
  
  Или одной командой в терминале:

```bash
    python3 -c "
    import zlib
    data = open('SOURCES.bin','rb').read()
    open('sources.zip','wb').write(zlib.decompress(data[17:]))"
    unzip sources.zip
```

- Извлечение данных из ZIP

```python
   import zipfile
   with zipfile.ZipFile("sources.zip") as zf:
    zf.extractall("sources/")
```

После этого в папке `sources/` появятся файлы примерно такой структуры:

```text
sources/
├── document.md
├── style/
│   └── style.css
└── images/
    └── *.png
```

- Пример для RAG

```python
from pathlib import Path
from langchain_text_splitters import RecursiveCharacterTextSplitter

# ... здесь ваш vector store / embedder

sources_dir = extract_sources_from_pdf("document.pdf", "sources")

# Ищем Markdown-файл внутри sources/.
md_files = list(Path(sources_dir).rglob("*.md"))
assert md_files, "Markdown не найден внутри архива"
text = md_files[0].read_text(encoding="utf-8")
splitter = RecursiveCharacterTextSplitter(chunk_size=800, chunk_overlap=100)
chunks = splitter.split_text(text)

# Дальше — как обычно: chunks → embeddings → vector store.
# Например:
# vs.add_texts(chunks, metadatas=[{"source": str(md_files[0])}] * len(chunks))
print(f"Готово: {len(chunks)} чанков из {md_files[0]}")
```
Картинки из `sources/images/` можно подключить отдельно для мультимодального поиска: индексируйте их вместе с текстом и храните путь к файлу в метаданных чанка.

- Кратко

1. Например, с помощью `pypdf` достаньте `SOURCES.bin` из PDF.
2. Отрежьте 17 байт заголовка и вызовите `zlib.decompress`, чтобы получить ZIP.
3. Извлеките архив с помощью `zipfile.extractall`.
4. Для RAG, возьмите Markdown-файл из архива, разбейте его на чанки и передайте в векторный индекс.

---

## Сборка из исходников

### Требования

- Ubuntu 22.04 или новее (сборка велась на 24.04).
- Пакеты: `build-essential pkg-config libgtk-3-dev zlib1g-dev libzip-dev libmupdf-dev`.

### Сборка

```bash
git clone https://github.com/JonGeorgia/pdf-builder.git
cd pdf-builder
make
```

После сборки появится бинарник `pdf-builder-c` который использует:

- `pandoc` — для конвертации Markdown → HTML;
- `weasyprint` — для конвертации HTML → PDF.
- `Python 3` с `matplotlib` — для рендеринга LaTeX-формул (см. `math2svg.py`).

Эти зависимости не входят в системные пакеты проекта. Рекомендуется использовать **виртуальное окружение Python**:

```bash
python3 -m venv venv
venv/bin/pip install -r requirements.txt
```

Список необходимых Python-пакетов — в файле [`requirements.txt`](requirements.txt) (в нём указаны `matplotlib` и `weasyprint`). После установки активируйте окружение и запускайте программу:

```bash
source venv/bin/activate
./pdf-builder-c
```

---

## Сторонние библиотеки и лицензии

PDFuse распространяется под лицензией **GNU GPL v3** (см. файл [LICENSE](LICENSE)).

Программа использует следующие сторонние библиотеки. Тексты их лицензий находятся в папке [`licenses/`](licenses/):

| Библиотека | Лицензия | Назначение |
|---|---|---|
| [MuPDF](https://mupdf.com/) | AGPL v3 | Работа с PDF |
| [pandoc](https://pandoc.org/) | GPL v2+ | Markdown → HTML |
| [WeasyPrint](https://weasyprint.org/) | BSD 3-Clause | HTML → PDF |
| [GTK 3](https://www.gtk.org/) | LGPL v2.1 | Графический интерфейс |
| [Python](https://www.python.org/) | PSF License 2 | Среда выполнения WeasyPrint |

Исходный код этих библиотек доступен по ссылкам выше.

---

## Обратная связь

- **Баги, вопросы, предложения:** [GitHub Issues](https://github.com/JonGeorgia/pdf-builder/issues).
- **Поддержать проект:**
  - [Boosty](https://boosty.to/automatonlab)
  - [Ko-fi](https://ko-fi.com/automatonlab)

---

## Лицензия

Copyright (C) AutomatonLab (JonGeorgia).

Программа распространяется под лицензией **GNU General Public License v3.0 или более поздней**. Полный текст — в файле [LICENSE](LICENSE).
