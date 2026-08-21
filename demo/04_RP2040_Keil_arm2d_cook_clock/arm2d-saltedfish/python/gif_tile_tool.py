"""
GIF逐帧平铺工具 - 用于动效预览/帧表制作，完全支持透明背景
========== 所有参数都在下面配置区改，不用动下面逻辑 ==========
"""
from PIL import Image, ImageDraw, ImageFont
import os

# ====================== 配置区（你直接改这里就行） ======================
INPUT_GIF_PATH = r"C:\Users\11791\OneDrive\Desktop\images\girl.gif"  # 输入GIF路径
OUTPUT_IMAGE_PATH = r"C:\Users\11791\OneDrive\Desktop\images\girl.png" # 输出平铺图路径
# 平铺配置
PER_ROW = 12                         # 每行放多少帧，自动计算总高度
FRAME_SPACING = 0                   # 帧之间的间距（像素）
SCALE = 1.0                          # 缩放比例，<1缩小，>1放大，1为原尺寸
# 美化配置
BORDER_WIDTH = 0                     # 每帧的边框宽度，0为不加边框
BORDER_COLOR = (255, 0, 0, 255)      # 边框颜色，(R,G,B,A)，默认红色
SHOW_INDEX = False                    # 是否在每帧下方显示帧编号
INDEX_FONT_SIZE = 12                 # 编号字体大小
INDEX_COLOR = (255, 255, 255, 255)   # 编号颜色，默认白色
TRANSPARENT_BACKGROUND = True        # 输出背景是否透明，False则为黑色背景
# =======================================================================

# 读取GIF所有帧
gif = Image.open(INPUT_GIF_PATH)
frames = []
try:
    while True:
        # 复制当前帧，保留透明通道
        frame = gif.copy().convert("RGBA")
        # 缩放
        if SCALE != 1.0:
            new_w = int(frame.width * SCALE)
            new_h = int(frame.height * SCALE)
            frame = frame.resize((new_w, new_h), Image.Resampling.NEAREST)
        frames.append(frame)
        gif.seek(gif.tell() + 1)
except EOFError:
    pass  # 读完所有帧
total_frames = len(frames)
print(f"读取到GIF共{total_frames}帧，单帧尺寸：{frames[0].width}×{frames[0].height}")

# 计算平铺图总尺寸
frame_w = frames[0].width
frame_h = frames[0].height
extra_height = INDEX_FONT_SIZE + 5 if SHOW_INDEX else 0
rows = (total_frames + PER_ROW - 1) // PER_ROW  # 向上取整计算行数
total_width = PER_ROW * frame_w + (PER_ROW - 1) * FRAME_SPACING + BORDER_WIDTH * 2
total_height = rows * (frame_h + extra_height) + (rows - 1) * FRAME_SPACING + BORDER_WIDTH * 2

# 创建画布
bg_mode = "RGBA" if TRANSPARENT_BACKGROUND else "RGB"
bg_color = (0,0,0,0) if TRANSPARENT_BACKGROUND else (0,0,0)
tile_img = Image.new(bg_mode, (total_width, total_height), bg_color)
draw = ImageDraw.Draw(tile_img)
# 加载字体（用系统默认无衬线字体）
try:
    font = ImageFont.truetype("arial.ttf", INDEX_FONT_SIZE)
except:
    font = ImageFont.load_default(size=INDEX_FONT_SIZE)

# 逐帧贴到画布
for idx, frame in enumerate(frames):
    row = idx // PER_ROW
    col = idx % PER_ROW
    # 计算当前帧的位置
    x = BORDER_WIDTH + col * (frame_w + FRAME_SPACING)
    y = BORDER_WIDTH + row * (frame_h + FRAME_SPACING + extra_height)
    # 贴帧
    tile_img.paste(frame, (x, y), frame)
    # 画边框
    if BORDER_WIDTH > 0:
        draw.rectangle(
            [x, y, x + frame_w, y + frame_h],
            outline=BORDER_COLOR,
            width=BORDER_WIDTH
        )
    # 写编号
    if SHOW_INDEX:
        text = f"#{idx+1}"
        # 计算文字居中位置
        text_bbox = draw.textbbox((0, 0), text, font=font)
        text_w = text_bbox[2] - text_bbox[0]
        text_x = x + (frame_w - text_w) // 2
        text_y = y + frame_h + 2
        draw.text((text_x, text_y), text, fill=INDEX_COLOR, font=font)
    print(f"已处理第{idx+1}/{total_frames}帧")

# 保存
tile_img.save(OUTPUT_IMAGE_PATH)
print(f"\n✅ 平铺图生成完成！路径：{OUTPUT_IMAGE_PATH}")
print(f"平铺尺寸：{total_width}×{total_height}，共{rows}行×{PER_ROW}列")
