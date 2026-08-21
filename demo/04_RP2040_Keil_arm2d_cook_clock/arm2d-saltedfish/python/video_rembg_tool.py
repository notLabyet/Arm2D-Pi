"""
视频抠图转序列帧工具 - 专为ARM2D动效/B站素材设计
========== 所有参数都在下面配置区改，不用动下面逻辑 ==========
"""
import warnings
warnings.filterwarnings("ignore") # 屏蔽无关性能警告
import cv2
from rembg import remove
from PIL import Image
import numpy as np
import os

# ====================== 配置区（你直接改这里就行） ======================
INPUT_VIDEO_PATH = r"C:\Users\11791\OneDrive\Desktop\images\girl.mp4"  # 输入视频路径
OUTPUT_DIR = r"C:\Users\11791\OneDrive\Desktop\images\output_frames"         # 输出帧保存路径
# 帧配置
TARGET_FRAME_COUNT = 100                 # 要输出的总帧数，均匀从视频里抽取
TARGET_SIZE = (240, 240)                # 输出图片尺寸（宽,高），和你LCD一致就行
AUTO_CROP_CENTER = True                 # 自动居中裁剪，避免拉伸变形，建议开
# 画质配置
CONTRAST = 1.0                          # 对比度倍数，>1提高，视频暗就往大调
BRIGHTNESS = 10                         # 亮度增加值，0~255，视频暗往大调
ENABLE_ALPHA_MATTING = False             # 开边缘优化，抠出来无锯齿
ONLY_OUTPUT_MASK = False                # 只输出黑白Alpha遮罩，做局部刷新时开
ENABLE_REMOVE_BG = True                 # 是否开启抠图，关闭后直接抽帧转GIF（速度提升10倍以上）
USE_GPU = False                         # 要是CUDA装好了就改成True，速度快10倍
# GIF输出配置
ENABLE_GIF_OUTPUT = True                # 是否输出合并后的GIF
OUTPUT_GIF_PATH = r"C:\Users\11791\OneDrive\Desktop\images\output.gif" # GIF输出路径
GIF_FPS = 10                            # GIF的播放帧率，10帧每秒就很流畅
GIF_LOOP_FOREVER = True                 # GIF是否无限循环
KEEP_FRAMES_AFTER_GIF = True            # 生成GIF后是否保留单独的序列帧
# =======================================================================

# 自动创建输出目录
os.makedirs(OUTPUT_DIR, exist_ok=True)
# 配置校验
if not ENABLE_REMOVE_BG and ONLY_OUTPUT_MASK:
    print("⚠️  提示：不抠图模式下ONLY_OUTPUT_MASK选项无效，已自动关闭")
    ONLY_OUTPUT_MASK = False
# 选择运行后端
providers = ['CUDAExecutionProvider', 'CPUExecutionProvider'] if USE_GPU else ['CPUExecutionProvider']

# 读取视频信息
cap = cv2.VideoCapture(INPUT_VIDEO_PATH, cv2.CAP_FFMPEG)
total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
# 计算要抽取的帧索引（均匀分布在整个视频）
extract_indices = [int(i * total_frames / TARGET_FRAME_COUNT) for i in range(TARGET_FRAME_COUNT)]

print(f"=== 开始处理 ===")
print(f"输入视频总帧数：{total_frames}")
print(f"将抽取{len(extract_indices)}帧，输出尺寸：{TARGET_SIZE[0]}×{TARGET_SIZE[1]}")
print(f"输出路径：{OUTPUT_DIR}\n")

for idx, frame_idx in enumerate(extract_indices):
    # 跳到指定帧
    cap.set(cv2.CAP_PROP_POS_FRAMES, frame_idx)
    ret, frame = cap.read()
    if not ret:
        print(f"跳过第{idx+1}帧：读取失败")
        continue
    
    # 1. 调整对比度亮度
    frame = cv2.convertScaleAbs(frame, alpha=CONTRAST, beta=BRIGHTNESS)
    
    # 2. 裁剪+缩放
    if AUTO_CROP_CENTER:
        h, w = frame.shape[:2]
        crop_size = min(w, h)
        x_start = (w - crop_size) // 2
        y_start = (h - crop_size) // 2
        frame = frame[y_start:y_start+crop_size, x_start:x_start+crop_size]
    frame = cv2.resize(frame, TARGET_SIZE)
    
    # 3. 抠图/直接使用原图
    frame_rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    img_pil = Image.fromarray(frame_rgb)
    if ENABLE_REMOVE_BG:
        img_rembg = remove(
            img_pil,
            providers=providers,
            alpha_matting=ENABLE_ALPHA_MATTING,
            alpha_matting_discard_threshold=1e-5,  # 优化矩阵计算，消除警告
            alpha_matting_shift=1e-2,
            only_mask=ONLY_OUTPUT_MASK
        )
    else:
        # 不抠图模式，直接保留原图，添加全不透明alpha通道兼容后续逻辑
        img_rembg = img_pil.convert("RGBA")
    
    # 4. 保存
    if ONLY_OUTPUT_MASK:
        # 遮罩直接存灰度图
        img_out = Image.fromarray(np.array(img_rembg) * 255).convert("L")
    else:
        # 存带透明通道的RGBA图
        img_out = Image.fromarray(np.array(img_rembg))
    
    output_path = os.path.join(OUTPUT_DIR, f"frame_{idx+1:02d}.png")
    img_out.save(output_path)
    print(f"已完成第{idx+1}/{TARGET_FRAME_COUNT}帧：{os.path.basename(output_path)}")

cap.release()
frame_count = len(os.listdir(OUTPUT_DIR))
print(f"\n=== 帧处理完成，共生成{frame_count}帧 ===")

# ========== 生成GIF ==========
if ENABLE_GIF_OUTPUT and frame_count > 0:
    print("正在生成GIF...")
    # 读取所有帧
    frames = []
    for i in range(1, frame_count+1):
        frame_path = os.path.join(OUTPUT_DIR, f"frame_{i:02d}.png")
        if os.path.exists(frame_path):
            img = Image.open(frame_path)
            # 处理透明通道，避免GIF透明异常
            if img.mode == "RGBA":
                # 转索引色，保留透明
                alpha = img.getchannel("A")
                img = img.convert("P", palette=Image.ADAPTIVE, colors=256)
                mask = Image.eval(alpha, lambda a: 255 if a <= 128 else 0)
                img.paste(255, mask=mask)
                img.info["transparency"] = 255
            frames.append(img)
    
    if len(frames) > 0:
        # 计算每帧延迟时间（毫秒）
        frame_delay = int(1000 / GIF_FPS)
        loop = 0 if GIF_LOOP_FOREVER else 1
        # 保存GIF
        frames[0].save(
            OUTPUT_GIF_PATH,
            save_all=True,
            append_images=frames[1:],
            duration=frame_delay,
            loop=loop,
            disposal=2, # 每帧播放完清除，避免透明残留
            optimize=False
        )
        print(f"GIF生成成功：{OUTPUT_GIF_PATH}")
        
        # 删除单独帧（如果配置了）
        if not KEEP_FRAMES_AFTER_GIF:
            for f in os.listdir(OUTPUT_DIR):
                os.remove(os.path.join(OUTPUT_DIR, f))
            os.rmdir(OUTPUT_DIR)
            print("已清理临时序列帧文件")

print(f"\n=== 全部处理完成 ===")
if KEEP_FRAMES_AFTER_GIF or not ENABLE_GIF_OUTPUT:
    print(f"序列帧保存在：{OUTPUT_DIR}")
if ENABLE_GIF_OUTPUT:
    print(f"GIF保存在：{OUTPUT_GIF_PATH}")
