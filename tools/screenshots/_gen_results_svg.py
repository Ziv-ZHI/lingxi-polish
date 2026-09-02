"""Generate docs/assets/results-handeye.svg from the smoke test numbers.

Numbers come from the demo verification harness (node demo/_smoke.js).
Re-run ``node demo/_smoke.js`` and update the data block below to refresh.
"""
import os

# === data block (from the latest `node demo/_smoke.js` run) ===
trials = [
    ("#1", 0.284, 1.8470, 71),
    ("#2", 0.257, 1.8520, 84),
    ("#3", 0.273, 1.8490, 85),
    ("#4", 0.279, 1.8530, 90),
    ("#5", 0.282, 1.8510, 81),
    ("#6", 0.270, 1.8460, 85),
]
MASS_TRUE = 1.850
RMS_MEAN = sum(r[1] for r in trials) / len(trials)
MASS_MEAN = sum(r[2] for r in trials) / len(trials)
MASS_MEAN_ERR = MASS_MEAN - MASS_TRUE
COV_MEAN = sum(r[3] for r in trials) / len(trials)

W, H = 720, 360
PAD_L, PAD_R, PAD_T, PAD_B = 70, 40, 50, 70
PLOT_W = W - PAD_L - PAD_R
PLOT_H = H - PAD_T - PAD_B

# y-axis: 0.20 .. 0.32 px
Y_MIN, Y_MAX = 0.20, 0.32

def y(v):
    return PAD_T + PLOT_H * (1 - (v - Y_MIN) / (Y_MAX - Y_MIN))

bar_w = PLOT_W / (len(trials) + 1) * 0.55
gap = (PLOT_W - bar_w * len(trials)) / (len(trials) + 1)

svg = []
svg.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" '
           f'width="{W}" height="{H}" role="img" '
           f'aria-label="Hand-eye reprojection RMS across 6 calibration trials" '
           f'font-family="system-ui, -apple-system, Segoe UI, Roboto, sans-serif">')
svg.append(f'<rect width="{W}" height="{H}" fill="#FFFFFF"/>')
svg.append(f'<text x="{PAD_L}" y="28" font-size="14" font-weight="600" fill="#17394D">'
           f'Hand-eye calibration — reprojection RMS (6 independent trials)</text>')
svg.append(f'<text x="{PAD_L}" y="46" font-size="11" fill="#3D5C6B">'
           f'mean = {RMS_MEAN:.3f} px  ·  sub-pixel  ·  generated from '
           f'<tspan font-family="Menlo, Consolas, monospace">demo/_smoke.js</tspan></text>')

# y-axis grid + labels
for v in [0.20, 0.22, 0.24, 0.26, 0.28, 0.30, 0.32]:
    yv = y(v)
    svg.append(f'<line x1="{PAD_L}" y1="{yv:.1f}" x2="{W-PAD_R}" y2="{yv:.1f}" '
               f'stroke="#E5E7EB" stroke-width="1"/>')
    svg.append(f'<text x="{PAD_L-8}" y="{yv+4:.1f}" text-anchor="end" '
               f'font-size="11" fill="#6B7280">{v:.2f}</text>')

# sub-pixel reference line at 1.0 px
y1 = y(1.0)
# (off-chart, skip)

# mean line
ymean = y(RMS_MEAN)
svg.append(f'<line x1="{PAD_L}" y1="{ymean:.1f}" x2="{W-PAD_R}" y2="{ymean:.1f}" '
           f'stroke="#C2410C" stroke-width="1.5" stroke-dasharray="5 4"/>')
svg.append(f'<text x="{W-PAD_R-4}" y="{ymean-6:.1f}" text-anchor="end" '
           f'font-size="11" fill="#C2410C">mean {RMS_MEAN:.3f} px</text>')

# bars
for i, (lbl, rms, mass, cov) in enumerate(trials):
    x = PAD_L + gap * (i + 1) + bar_w * i
    yv = y(rms)
    svg.append(f'<rect x="{x:.1f}" y="{yv:.1f}" width="{bar_w:.1f}" '
               f'height="{PAD_T+PLOT_H-yv:.1f}" fill="#2E5C6E" rx="2"/>')
    svg.append(f'<text x="{x+bar_w/2:.1f}" y="{yv-6:.1f}" text-anchor="middle" '
               f'font-size="11" font-weight="600" fill="#17394D">{rms:.3f}</text>')
    svg.append(f'<text x="{x+bar_w/2:.1f}" y="{PAD_T+PLOT_H+18:.1f}" text-anchor="middle" '
               f'font-size="11" fill="#3D5C6B">{lbl}</text>')
    svg.append(f'<text x="{x+bar_w/2:.1f}" y="{PAD_T+PLOT_H+34:.1f}" text-anchor="middle" '
               f'font-size="10" fill="#6B7280">m={mass:.4f} kg</text>')
    svg.append(f'<text x="{x+bar_w/2:.1f}" y="{PAD_T+PLOT_H+48:.1f}" text-anchor="middle" '
               f'font-size="10" fill="#6B7280">cov {cov}%</text>')

# axes
svg.append(f'<line x1="{PAD_L}" y1="{PAD_T+PLOT_H}" x2="{W-PAD_R}" y2="{PAD_T+PLOT_H}" '
           f'stroke="#374151" stroke-width="1.2"/>')
svg.append(f'<line x1="{PAD_L}" y1="{PAD_T}" x2="{PAD_L}" y2="{PAD_T+PLOT_H}" '
           f'stroke="#374151" stroke-width="1.2"/>')
# axis labels
svg.append(f'<text x="{PAD_L}" y="{H-12}" font-size="11" fill="#374151">Trial (payload mass = 1.850 kg truth)</text>')
svg.append(f'<text transform="translate(20 {PAD_T+PLOT_H/2}) rotate(-90)" '
           f'font-size="11" fill="#374151" text-anchor="middle">reprojection RMS (px)</text>')
svg.append('</svg>')

out = os.path.join(os.path.dirname(__file__), "..", "..", "docs", "assets", "results-handeye.svg")
out = os.path.abspath(out)
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, "w", encoding="utf-8") as f:
    f.write("\n".join(svg))
print("wrote", out)
