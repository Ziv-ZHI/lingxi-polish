# -*- coding: utf-8 -*-
"""Minimal, dependency-free HTML -> .docx generator (stdlib only).
Supports: h1/h2/h3, p, b/strong, ul/li, table/tr/td/th, code, br.
Chinese font set to Microsoft YaHei (eastAsia). No network needed.
"""
import sys, re, zipfile, html
from html.parser import HTMLParser
from xml.sax.saxutils import escape

FONT = "Microsoft YaHei"

class DocxBuilder(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.els = []          # list of element tuples
        self._runs = []        # current paragraph runs: (text, bold)
        self._in_block = False # inside a block that owns _runs
        self._bold = 0
        self._skip = 0         # skip depth (style/script)
        self._table = None     # current table rows
        self._row = None
        self._cell = None      # current cell runs
        self._list = False

    def _flush_para(self, style=None):
        if style is not None or self._runs:
            self.els.append(("p", style, self._runs))
        self._runs = []

    def handle_starttag(self, tag, attrs):
        if tag in ("script", "style"):
            self._skip += 1
            return
        if self._skip:
            return
        if tag in ("h1", "h2", "h3"):
            self._flush_para()
            self._in_block = True
            self._cur_style = {"h1": "Heading1", "h2": "Heading2", "h3": "Heading3"}[tag]
        elif tag == "p":
            self._flush_para()
            self._in_block = True
        elif tag == "li":
            self._flush_para()
            self._in_block = True
            self._runs.append(("•  ", False))
        elif tag in ("b", "strong"):
            self._bold += 1
        elif tag == "br":
            self._runs.append(("\n", False))
        elif tag == "code":
            pass  # treat as normal text styling below via class; keep simple
        elif tag == "ul":
            self._list = True
        elif tag == "table":
            self._flush_para()
            self._table = []
        elif tag == "tr":
            self._row = []
        elif tag in ("td", "th"):
            self._cell = []
            self._cell_bold = (tag == "th")
        elif tag == "div":
            # divs in our docs are layout-only; treat block start as paragraph break if it had text
            pass

    def handle_endtag(self, tag):
        if tag in ("script", "style"):
            self._skip = max(0, self._skip - 1)
            return
        if self._skip:
            return
        if tag in ("h1", "h2", "h3"):
            self._flush_para(self._cur_style)
            self._in_block = False
        elif tag == "p":
            self._flush_para()
            self._in_block = False
        elif tag == "li":
            self._flush_para()
        elif tag in ("b", "strong"):
            self._bold = max(0, self._bold - 1)
        elif tag == "ul":
            self._list = False
        elif tag == "tr":
            if self._row is not None:
                self._table.append(self._row)
            self._row = None
        elif tag in ("td", "th"):
            if self._row is not None and self._cell is not None:
                self._row.append(("c", self._cell_bold, self._cell))
            self._cell = None
        elif tag == "table":
            if self._table is not None:
                self.els.append(("table", None, self._table))
            self._table = None

    def handle_data(self, data):
        if self._skip:
            return
        t = data
        if self._cell is not None:
            self._cell.append((t, self._bold > 0 or self._cell_bold))
        elif self._in_block or self._runs or t.strip():
            self._runs.append((t, self._bold > 0))

def run_xml(text, bold):
    b = "<w:b/>" if bold else ""
    rpr = f'<w:rPr><w:rFonts w:ascii="{FONT}" w:hAnsi="{FONT}" w:eastAsia="{FONT}"/>{b}</w:rPr>'
    return f'<w:r>{rpr}<w:t xml:space="preserve">{escape(text)}</w:t></w:r>'

def para_xml(style, runs):
    ppr = f'<w:pPr><w:pStyle w:val="{style}"/></w:pPr>' if style else "<w:pPr/>"
    body = "".join(run_xml(t, b) for (t, b) in runs) or run_xml("", False)
    return f"<w:p>{ppr}{body}</w:p>"

def cell_xml(cell):
    _, bold, runs = cell
    # merge consecutive; keep as one paragraph
    ppr = "<w:pPr/>"
    body = "".join(run_xml(t, b) for (t, b) in runs) or run_xml("", False)
    return f'<w:tc><w:tcPr><w:tcW w:w="0" w:type="auto"/></w:tcPr><w:p>{ppr}{body}</w:p></w:tc>'

def table_xml(rows):
    borders = "".join(
        f'<w:{e} w:val="single" w:sz="4" w:space="0" w:color="999999"/>'
        for e in ("top", "left", "bottom", "right", "insideH", "insideV")
    )
    trs = ""
    for row in rows:
        tds = "".join(cell_xml(c) for c in row)
        trs += f"<w:tr>{tds}</w:tr>"
    return (f'<w:tbl><w:tblPr><w:tblW w:w="0" w:type="auto"/>'
            f'<w:tblBorders>{borders}</w:tblBorders></w:tblPr>{trs}</w:tbl>')

def build_document(els):
    out = []
    for el in els:
        if el[0] == "p":
            out.append(para_xml(el[1], el[2]))
        elif el[0] == "table":
            out.append(table_xml(el[2]))
    sect = ('<w:sectPr><w:pgSz w:w="11906" w:h="16838"/>'
            '<w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440" '
            'w:header="720" w:footer="720" w:gutter="0"/></w:sectPr>')
    return "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" " \
           "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">" \
           f"<w:body>{''.join(out)}{sect}</w:body></w:document>"

STYLES = '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:styles xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii="Microsoft YaHei" w:hAnsi="Microsoft YaHei" w:eastAsia="Microsoft YaHei"/><w:sz w:val="21"/></w:rPr></w:rPrDefault></w:docDefaults>
<w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/><w:pPr><w:spacing w:after="120" w:line="288" w:lineRule="auto"/></w:pPr></w:style>
<w:style w:type="paragraph" w:styleId="Heading1"><w:name w:val="heading 1"/><w:basedOn w:val="Normal"/><w:pPr><w:spacing w:before="240" w:after="120"/><w:outlineLvl w:val="0"/></w:pPr><w:rPr><w:b/><w:sz w:val="32"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="Heading2"><w:name w:val="heading 2"/><w:basedOn w:val="Normal"/><w:pPr><w:spacing w:before="200" w:after="100"/><w:outlineLvl w:val="1"/></w:pPr><w:rPr><w:b/><w:sz w:val="26"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="Heading3"><w:name w:val="heading 3"/><w:basedOn w:val="Normal"/><w:pPr><w:spacing w:before="160" w:after="80"/><w:outlineLvl w:val="2"/></w:pPr><w:rPr><w:b/><w:sz w:val="23"/></w:rPr></w:style>
</w:styles>'''

CONTENT_TYPES = '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
<Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/>
</Types>'''

RELS = '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
</Relationships>'''

DOC_RELS = '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>
</Relationships>'''

def html_to_docx(html_path, docx_path):
    data = open(html_path, encoding="utf-8").read()
    # only body
    m = re.search(r"<body>(.*)</body>", data, re.S)
    body = m.group(1) if m else data
    p = DocxBuilder()
    p.feed(body)
    p._flush_para()
    doc_xml = build_document(p.els)
    with zipfile.ZipFile(docx_path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", CONTENT_TYPES)
        z.writestr("_rels/.rels", RELS)
        z.writestr("word/document.xml", doc_xml)
        z.writestr("word/styles.xml", STYLES)
        z.writestr("word/_rels/document.xml.rels", DOC_RELS)
    return docx_path

if __name__ == "__main__":
    for src, dst in [
        ("docs/program-intro.html", "docs/程序简介.docx"),
        ("docs/dev-log.html", "docs/开发日志.docx"),
    ]:
        html_to_docx(src, dst)
        print("wrote", dst)
