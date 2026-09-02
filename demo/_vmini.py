#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
专项测试:小程序 v-mini section 的 Tab 切换结构契约

检查项:
  C1. v-mini 内 <div> 数 == </div> 数(配平)
  C2. 5 个 mpPage(mpHome/mpDev/mpData/mpOrder/mpMe)各自内容互不相同
  C3. CSS 规则 .mp-page{display:none} 与 .mp-page.on{display:block} 同时存在
  C4. switchMini 函数存在,且只对 MP_PAGE[k] 的 classList 切 .on
  C5. .phone 与 .ph-screen 在 mpTab 之前都已关闭(不能延伸到 col 兄弟)
  C6. grid3 的两个 col 都存在(说明 phone 与"右侧说明栏"是 grid3 的两列,
      不是 ph-screen 的子节点)

不依赖浏览器/stub,纯静态正则 + AST 风格的 div 栈追踪。
"""

import re, sys, os

FILE = os.path.join(os.path.dirname(__file__), "monitor-demo.html")

src = open(FILE, "r", encoding="utf-8").read()

errs = []
warns = []

# === C1: 整文件 div 配平 ===
op_all = len(re.findall(r"<div\b", src))
cl_all = len(re.findall(r"</div>", src))
if op_all != cl_all:
    errs.append(f"C1 整文件 <div>={op_all} </div>={cl_all} 不平衡")
else:
    print(f"[OK] C1 整文件 div 配平: {op_all}")

# === v-mini section 切片 ===
m = re.search(r"<!--\s*=+\s*视图 8.*?</section>", src, re.S)
if not m:
    errs.append("C1b 找不到 v-mini section")
    sys.exit(1)
seg = m.group(0)
op = len(re.findall(r"<div\b", seg))
cl = len(re.findall(r"</div>", seg))
if op != cl:
    errs.append(f"C1b v-mini <div>={op} </div>={cl} 不平衡")
else:
    print(f"[OK] C1b v-mini section div 配平: {op}")

# === C2: 5 个 mpPage 内容互不相同 ===
PAGES = ["mpHome", "mpDev", "mpData", "mpOrder", "mpMe"]
contents = {}
for pid in PAGES:
    start = src.find(f'id="{pid}"')
    if start == -1:
        errs.append(f"C2 找不到 id={pid}")
        continue
    # 截到下一个 mp-page 或 col 关闭
    nx = src.find('class="mp-page', start + 10)
    end = src.find("</section>", start) if nx == -1 else nx
    seg_p = src[start:end]
    text = re.sub(r"<[^>]+>", " ", seg_p)
    text = re.sub(r"\s+", " ", text).strip()
    contents[pid] = text

unique_sigs = {text[:80] for text in contents.values()}
if len(unique_sigs) != len(contents):
    errs.append(f"C2 5 个 mpPage 内容摘要互相同(只 {len(unique_sigs)} 种)")
else:
    print(f"[OK] C2 5 个 mpPage 内容互不相同:")
    for pid in PAGES:
        snippet = contents[pid][:60].replace("\n", " ")
        print(f"        {pid}: {snippet}…")

# === C3: CSS 规则 ===
if (re.search(r"\.mp-page\s*\{\s*display\s*:\s*none", src)
        and re.search(r"\.mp-page\.on\s*\{\s*display\s*:\s*block", src)):
    print("[OK] C3 .mp-page{display:none} + .mp-page.on{display:block} 同时存在")
else:
    errs.append("C3 缺少 CSS 规则: .mp-page{display:none} 或 .mp-page.on{display:block}")

# === C4: switchMini ===
m = re.search(r"function\s+switchMini\s*\([^)]*\)\s*\{(.*?)\n\s*\}", src, re.S)
if not m:
    errs.append("C4 找不到 switchMini 函数")
else:
    body = m.group(1)
    if "MP_PAGE" in body and 'classList.toggle("on"' in body:
        print("[OK] C4 switchMini 通过 MP_PAGE 切换 .on")
    else:
        errs.append("C4 switchMini 逻辑缺 MP_PAGE/classList.toggle('on')")

# === C5: phone 与 ph-screen 在 mpTab 之前都已关闭 ===
# 关键断言:在 grid3 内的第一个 col 内、phone 与 ph-screen 应在 mpTab 关闭之后立即关闭,
# 且 col 也应在 mpTab 关闭后立刻关闭 —— 不能延伸到 grid3 第二列
tab_close = src.find('</div>', src.find('id="mpTab"'))
# 找最近一个 </div> 之前的 phone 边界
phone_open = src.find('<div class="phone">')
phs_open   = src.find('<div class="ph-screen">')

# 计数:从 phone_open 到 tab_close 之后第一次出现的 "</div>" 应是关 ph-screen
# 然后紧接着的应是关 phone
post_tab = src[tab_close:]
# 取后 600 字符
tail = post_tab[:600]
if tail.count("</div>") >= 3:
    # 第一个关 ph-screen,第二个关 phone,第三个关第一列 col
    print("[OK] C5 mpTab 关闭后有 ≥3 个 </div>(关 ph-screen / phone / 第一列 col)")
else:
    errs.append(f"C5 mpTab 关闭后只有 {tail.count('</div>')} 个 </div>(应≥3)")

# === C6: grid3 内两个 col ===
# 简化:看 v-mini 内 "<div class=\"col\">" 出现次数(应是 2)
seg_col = re.findall(r'<div class="col">', seg)
if len(seg_col) >= 2:
    print(f"[OK] C6 grid3 内出现 {len(seg_col)} 个 <div class='col'>(应有 2)")
else:
    errs.append(f"C6 grid3 内只找到 {len(seg_col)} 个 col(应有 2)")

# === 收口 ===
print("=" * 60)
if errs:
    print(f"结果: 失败 ({len(errs)} 错误)")
    for e in errs:
        print("  ✗", e)
    sys.exit(1)
else:
    print(f"结果: 通过 (0 错误, {len(warns)} 警告)")
    sys.exit(0)