# -*- coding: utf-8 -*-
"""校验 monitor-demo.html：DOM id 引用一致性 + 视图/导航一致性 + canvas 绘制覆盖
   + 小程序交互层契约（Tab 内联 / 状态机 / 写操作走下发通道）"""
import re, sys, io, os

BASE = os.path.dirname(os.path.abspath(__file__))
HTML = os.path.join(BASE, "monitor-demo.html")

src = io.open(HTML, encoding="utf-8").read()
errs, warns = [], []

m = re.search(r"<script>(.*?)</script>", src, re.S)
if not m:
    print("FATAL: 未找到 <script> 块"); sys.exit(1)
js = m.group(1)
html_body = src[:m.start()]

# ---------- id 一致性 ----------
defined_ids = set(re.findall(r'\bid="([A-Za-z0-9_]+)"', html_body))

list_m = re.search(r"\]\.forEach\(function \(id\) \{\s*el\[id\]", js)
el_ids = set(re.findall(r'"([A-Za-z0-9_]+)"', js[:list_m.start()])) if list_m else set()
direct_ids = set(re.findall(r'getElementById\("([A-Za-z0-9_]+)"\)', js))
# 小程序页面 id 通过 MP_PAGE = {home:"mpHome",...} 间接引用
indirect_ids = set(re.findall(r'"(mp[A-Z][A-Za-z0-9_]*)"', js))
js_ids = el_ids | direct_ids | indirect_ids

missing = sorted(js_ids - defined_ids)
unused = sorted(defined_ids - js_ids)
if missing: errs.append("JS 引用了 HTML 中不存在的 id: %s" % ", ".join(missing))
if unused:  warns.append("HTML 中定义但 JS 未引用的 id: %s" % ", ".join(unused))

# set()/val() 字面量引用（跳过 set("c" + key + "A") 这类拼接）
used_keys = set(re.findall(r'\bset\("([A-Za-z0-9_]+)",', js)) | \
            set(re.findall(r'\bval\("([A-Za-z0-9_]+)"\)', js))
bad = sorted(used_keys - js_ids)
if bad: errs.append("set()/val() 引用了未绑定的 id: %s" % ", ".join(bad))

# ---------- 视图 / 导航 / 元数据 ----------
views = set(re.findall(r'<section class="view[^"]*" id="v-([a-z]+)"', html_body))
navs  = set(re.findall(r'<button data-view="([a-z]+)"', html_body))
meta  = set(re.findall(r'^\s{4}([a-z]+):\["', js, re.M))
if navs - views: errs.append("导航指向不存在的视图: %s" % ", ".join(sorted(navs - views)))
if views - navs: warns.append("视图无导航入口: %s" % ", ".join(sorted(views - navs)))
if navs - meta:  errs.append("VIEW_META 缺少条目: %s" % ", ".join(sorted(navs - meta)))

# ---------- canvas 绘制覆盖 ----------
canvas_views = {}
for vm in re.finditer(r'<section class="view[^"]*" id="v-([a-z]+)"(.*?)</section>', html_body, re.S):
    cvs = re.findall(r'<canvas id="([A-Za-z0-9_]+)"', vm.group(2))
    if cvs: canvas_views[vm.group(1)] = cvs

CANVAS_FN = {
    "cForce":"drawForce", "cJoint":"drawJoint", "cPose":"drawPose", "cPose2":"drawPose",
    "cBar":"drawBar", "cStep":"drawStep", "cPath":"drawPath", "cCompare":"drawCompare",
    "cArch":"drawArch", "cCam":"drawCam", "cReproj":"drawReproj",
    "cMini":"drawMini", "cMiniBar":"drawMiniBar", "cMiniDonut":"drawMiniDonut",
}
da = re.search(r"function drawAll\(\)\{(.*?)\n  \}", js, re.S)
if not da:
    errs.append("未解析到 drawAll 函数体")
else:
    da_body = da.group(1)
    for v, cvs in sorted(canvas_views.items()):
        branch = re.search(r'VIEW === "%s"\)\s*\{(.*?)\n    \}' % v, da_body, re.S)
        if not branch:
            branch = re.search(r'VIEW === "%s"\)\s*\{(.*?)\}' % v, da_body, re.S)
        if not branch:
            errs.append("drawAll 未覆盖含 canvas 的视图 '%s'" % v); continue
        for cv in cvs:
            fn = CANVAS_FN.get(cv)
            if not fn:          errs.append("canvas '%s' 未登记绘制函数" % cv)
            elif fn + "(" not in branch.group(1):
                errs.append("视图 '%s' 的 canvas '%s' 未调用 %s()" % (v, cv, fn))

# ---------- 关键函数定义 ----------
for fn in ["drawForce","drawJoint","drawPose","drawBar","drawStep","drawPath","drawCompare",
           "drawArch","drawCam","drawReproj","drawMini","drawMiniBar","drawMiniDonut",
           "grabPose","solveHandEye","clearCalib","grabForce","solveGravity",
           "gaussSolve4","gravityDir","updateCalibUI","switchMini",
           "setup","plot","grid","roundRect",
           # 小程序端：渲染 / 交互组件 / 状态机 / 导出
           "renderMp","renderMpAlarms","renderMpDevices","renderMpOrders","renderMpTodo",
           "mpToast","mpSend","mpCanWrite","mpLog","woAdd","woMove","woFind",
           "mpAlarmToWo","mpOrderOwner","mpOrderNote",
           "mpBuildCsv","mpExportCsv","csvCell","fixFor","hourlyData","barData",
           "mpDevStatus","unackCount","openOrderCount"]:
    if not re.search(r"function %s\(" % fn, js):
        errs.append("缺少函数定义: %s" % fn)

# ---------- 小程序 Tab 与页面 id ----------
mp_page_ids = set(re.findall(r'<div class="mp-page[^"]*" id="(mp[A-Za-z0-9_]+)"', html_body))
mp_pages    = set(x.lower() for x in mp_page_ids)
mp_tabs     = set(re.findall(r'<button[^>]*data-tab="([A-Za-z0-9_]+)"', html_body))
mp_tabs     = set(t.lower() for t in mp_tabs)
mp_map      = dict((k.lower(), ("mp" + v).lower()) for k, v in re.findall(r'(\w+):"mp(\w+)"', js))

if mp_tabs - set(p[2:] for p in mp_pages):
    errs.append("TabBar 指向不存在的小程序页面: %s" % ", ".join(sorted(mp_tabs - set(p[2:] for p in mp_pages))))
if set(mp_map.keys()) - mp_tabs:
    errs.append("MP_PAGE 存在无对应 Tab 的条目: %s" % ", ".join(sorted(set(mp_map.keys()) - mp_tabs)))
if set(mp_map.values()) - mp_pages:
    errs.append("MP_PAGE 指向不存在的页面 id: %s" % ", ".join(sorted(set(mp_map.values()) - mp_pages)))
if mp_tabs - set(mp_map.keys()):
    errs.append("Tab 缺少 MP_PAGE 映射: %s" % ", ".join(sorted(mp_tabs - set(mp_map.keys()))))

# ---------- 小程序交互层契约（已简化为 Tab 内联，无二级页/弹层） ----------
# 1) 工单状态机必须闭合：每个非终态都要有后继
wo_st   = set(re.findall(r'^\s{4}(\w+):\s*\{ t:"', js, re.M))
wo_next = dict(re.findall(r'(\w+):"(\w+)",?\s*doing', js)[:0] or
               re.findall(r'(\w+):"(\w+)"', re.search(r"var WO_NEXT = \{(.*?)\};", js, re.S).group(1)))
for k in sorted(wo_st):
    if k != "done" and k not in wo_next:
        errs.append("工单状态 '%s' 在 WO_NEXT 中没有后继状态" % k)
for k in sorted(wo_next):
    if wo_next[k] not in wo_st:
        errs.append("WO_NEXT 指向未定义的状态: %s -> %s" % (k, wo_next[k]))

# 3) 写操作必须走下发通道（不允许直接改 state）
for pat, why in [(r'el\.mp(\w+)\.addEventListener\("click", function \(\) \{\s*el\.btn\w+\.click\(\); \}\);',
                  "小程序按钮直接代理主按钮，未走 mpSend 回执通道")]:
    if re.search(pat, js):
        errs.append(why)

# 4) Toast 节点齐备（覆盖层已移除，仅保留轻提示）
for nid in ["mpToast"]:
    if nid not in defined_ids:
        errs.append("缺少交互组件节点: %s" % nid)

# ---------- CSS 定义覆盖（HTML 与 JS 里用到的 mp- 类都要有样式） ----------
css_block = src[src.find("<style>"):src.find("</style>")]
css_classes = set(re.findall(r'\.(mp-[A-Za-z0-9_-]+)', css_block))
cls = set()
for u in re.findall(r'class="([^"]*mp-[^"]*)"', html_body) + \
         re.findall(r'class="([^"]*mp-[^"]*)"', js):
    for c in u.split():
        # JS 里是字符串拼接，类名可能带残留引号，先剥离再判断
        c = c.strip("'\" +")
        if c.startswith("mp-"): cls.add(c)
for u in re.findall(r'classList\.(?:add|toggle|remove)\("(mp-[A-Za-z0-9_-]+)"', js):
    cls.add(u)
for u in re.findall(r'"(mp-[A-Za-z0-9_-]+)(?:\s|")', js):
    cls.add(u)
undef = sorted(cls - css_classes)
if undef: errs.append("使用了未定义的 mp- 样式类: %s" % ", ".join(undef))

# ---------- 花括号平衡 ----------
if src.count("{") != src.count("}"):
    errs.append("花括号不平衡: { %d vs } %d" % (src.count("{"), src.count("}")))

print("=" * 62)
print("文件: %s" % HTML)
print("HTML %d 字符 | JS %d 字符" % (len(src), len(js)))
print("id: 定义 %d / 引用 %d" % (len(defined_ids), len(js_ids)))
print("视图(%d): %s" % (len(views), ", ".join(sorted(views))))
print("小程序页(%d): %s" % (len(mp_pages), ", ".join(sorted(mp_pages))))
print("二级页: 已移除（改为 Tab 内联）")
print("工单状态机: %s" % " → ".join(sorted(wo_st)))
print("-" * 62)
for w in warns: print("[WARN] " + w)
for e in errs:  print("[FAIL] " + e)
print("结果: %s" % ("失败 (%d)" % len(errs) if errs else "通过 (0 错误, %d 警告)" % len(warns)))
print("=" * 62)
sys.exit(1 if errs else 0)
