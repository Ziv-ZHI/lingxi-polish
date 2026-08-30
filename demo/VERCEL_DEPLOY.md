# 部署到 Vercel（灵犀智磨 BURNISH 上位机演示）

> **你已选择：连 GitHub 自动部署**（见下方「推荐路线」）。本仓库已 `git init` 并完成初始提交
> `0bfb4b6`，工作区干净，直接走下面的 4 步即可上线。

## 推荐路线：GitHub 自动部署（已选，最省事）

```bash
# 1. 在 GitHub 新建一个空仓库（不要勾 README/.gitignore，避免和本地冲突），拿到地址 <REPO_URL>

# 2. 关联并推送到 main（在本机执行，仓库已初始化好）
git remote add origin <REPO_URL>
git branch -M main
git push -u origin main

# 3. 打开 https://vercel.com/new → Import 该仓库
# 4. 配置（关键三项）：
#      Root Directory   = demo
#      Framework Preset = Other（纯静态，无构建）
#      Build Command    = 留空
#      Output Directory = demo
#    Deploy → 得到 https://<项目名>.vercel.app
```

> 推送前请先把仓库级 git 邮箱改成你自己的 GitHub 邮箱（否则贡献图不记名）：
> `git config user.email "你的GitHub邮箱"`（当前占位为 chenxin@example.com）。
> 改完重跑一次 `git commit --amend --reset-author` 或重新 push 即可。

---

本目录 `LingxiPolish/demo/` 是一个**零外部依赖的单文件 HTML**（`monitor-demo.html`，214 KB，
纯内联 CSS/JS，无任何外链），因此作为 Vercel 静态站点发布即可，无需构建。

已就绪的配置文件：
- `vercel.json` —— 用 rewrite 把任意路径都指向 `monitor-demo.html`，
  避免复制出第二份 `index.html` 造成内容漂移（改 `monitor-demo.html` 即生效）。
- `monitor-demo.html` —— 演示本体（v1.4.0，含小程序端 5 个 Tab 独立内容）。

> 注意：演示当前版本是 v1.4.0，已修复「小程序 Tab 切不动」的结构 bug（phone/ph-screen
> 容器此前未正确关闭，导致网页端右侧说明卡被渲染进手机屏幕）。

---

## 方式 A：本地用 Vercel CLI 部署（推荐，最简单）

```bash
# 1. 安装 CLI（如未装）
npm i -g vercel

# 2. 进入演示目录
cd LingxiPolish/demo

# 3. 登录（浏览器一次性授权，后续免登录）
vercel login

# 4. 部署到生产环境（首次会让你确认项目名/作用域）
vercel --prod
#   仅想先看预览： vercel
```

部署成功后终端会给出形如 `https://lingxi-polish-xxx.vercel.app` 的地址。

---

## 方式 B：连 GitHub 仓库，Vercel 自动构建

1. 把整个 `LingxiPolish` 仓库（或只含 `demo/` 的仓库）推到 GitHub。
2. 打开 https://vercel.com/new → Import 该仓库。
3. 配置：
   - Framework Preset：**Other**（纯静态）
   - Root Directory：`demo`（即含 `vercel.json` 的目录）
   - Build Command：**留空**
   - Output Directory：`demo`（默认即当前目录，Vercel 会直接托管静态文件）
4. Deploy。之后每次 push 自动重新发布。

---

## 方式 C：给我一个 Vercel Token，由我直接推送（无浏览器场景）

如果你希望我在本环境直接完成部署，需要你提供一个 Vercel Token：
- 生成地址：https://vercel.com/account/tokens （选 `Full Account` 或仅该项目权限）
- 拿到后执行：
  ```bash
  vercel deploy --prebuilt --prod --token "<你的TOKEN>"
  ```
  或用环境变量：
  ```bash
  VERCEL_TOKEN="<你的TOKEN>" vercel deploy --prebuilt --prod
  ```

---

## 常见问题

- **空白页 / 404**：确认部署 Root Directory 指向 `demo/`，且 `vercel.json` 已上传。
- **想换默认域名**：Vercel 控制台 → Project → Settings → Domains 绑定自有域名。
- **更新内容**：改 `monitor-demo.html` 后重新 `vercel --prod`（或 push 触发自动部署）。
  无需动 `vercel.json`。
- **本地预览服务**（非 Vercel，仅供本机看效果）：
  `python -m http.server 8123` 后访问 `http://127.0.0.1:8123/monitor-demo.html?v=4`
