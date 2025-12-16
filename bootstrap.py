import os
import sys
import subprocess
import venv
import shutil
from pathlib import Path

# ================= 配置 =================
# 虚拟环境目录名
VENV_DIR_NAME = ".venv"
# 依赖文件
REQUIREMENTS_FILE = "requirements.txt"
# 必须的包 (如果 requirements.txt 不存在，则安装这些)
# 这些是脚本运行的硬性依赖，必须安装到 venv 里
REQUIRED_PACKAGES = ["rich", "tomli"]
# =======================================

def get_venv_paths(root_dir: Path):
    """获取虚拟环境的路径，处理跨平台差异"""
    venv_dir = root_dir / VENV_DIR_NAME
    if sys.platform == "win32":
        python_executable = venv_dir / "Scripts" / "python.exe"
        pip_executable = venv_dir / "Scripts" / "pip.exe"
    else:
        python_executable = venv_dir / "bin" / "python"
        pip_executable = venv_dir / "bin" / "pip"
    return venv_dir, python_executable, pip_executable

def create_venv_if_missing(venv_dir: Path):
    """创建虚拟环境"""
    if not venv_dir.exists():
        print(f"[Bootstrap] Creating virtual environment at: {venv_dir}...", flush=True)
        # with_pip=True 确保 venv 里自带 pip，这是关键
        venv.create(venv_dir, with_pip=True)

def install_dependencies(root_dir: Path, pip_executable: Path):
    """
    使用【指定的 pip 路径】安装依赖。
    这里绝对不会调用系统的 pip，而是调用 venv 里的 pip。
    """
    req_path = root_dir / REQUIREMENTS_FILE
    
    # 构造 pip 命令基础部分 (使用清华源加速)
    # 禁用 pip 的版本检查警告，保持输出清爽
    base_cmd = [str(pip_executable), "install", "--disable-pip-version-check", "-i", "https://pypi.tuna.tsinghua.edu.cn/simple"]
    
    try:
        if req_path.exists():
            print(f"[Bootstrap] Installing dependencies from {REQUIREMENTS_FILE} into venv...", flush=True)
            subprocess.run(base_cmd + ["-r", str(req_path)], check=True)
        else:
            print(f"[Bootstrap] {REQUIREMENTS_FILE} not found. Installing base requirements ({', '.join(REQUIRED_PACKAGES)}) into venv...", flush=True)
            subprocess.run(base_cmd + REQUIRED_PACKAGES, check=True)
    except subprocess.CalledProcessError as e:
        print(f"[Bootstrap] Error: Failed to install dependencies into venv.", file=sys.stderr)
        print(f"[Bootstrap] Pip command failed with exit code {e.returncode}", file=sys.stderr)
        sys.exit(1)

def initialize():
    """
    引导程序主入口
    """
    # 1. 尝试导入依赖 (Fast Path)
    # 如果当前环境(无论是系统还是venv)已经有了这些包，直接继续，速度最快
    try:
        import rich
        import tomli
        return
    except ImportError:
        pass # 缺少依赖，需要处理

    # 获取路径
    root_dir = Path(__file__).parent.absolute()
    venv_dir, venv_python, venv_pip = get_venv_paths(root_dir)

    # 2. 判断我们是否已经在【脚本指定的】venv 里面运行
    # 通过环境变量标记来判断，避免无限递归
    is_in_venv = os.environ.get("GRADER_BOOTSTRAPPED") == "1"

    if is_in_venv:
        # 【情况A】：已经在 venv 里了，但 import 还是失败
        # 说明 venv 是空的或者坏了。尝试修复（安装依赖）。
        # 注意：这里我们使用 sys.executable 确保是用当前运行的 Python (即 venv python) 的 pip
        print("[Bootstrap] In venv but dependencies missing. Attempting to repair...", flush=True)
        install_dependencies(root_dir, venv_pip)
        # 安装完后，函数返回，脚本继续执行，此时后续的 import 应该能成功
        return

    # 【情况B】：在系统 Python (或错误的 venv) 中运行
    # 策略：确保 venv 存在 -> 确保 venv 里装了包 -> 用 venv 重启脚本
    
    # 2.1 确保 venv 存在
    create_venv_if_missing(venv_dir)

    # 2.2 确保依赖已安装到 venv 中
    # 注意：我们显式调用 venv_pip，这避开了“系统管理 Python”的问题
    install_dependencies(root_dir, venv_pip)
    
    # 2.3 重启脚本
    print("[Bootstrap] Restarting script inside virtual environment...", flush=True)
    
    env = os.environ.copy()
    env["GRADER_BOOTSTRAPPED"] = "1"
    
    # 传递所有原始参数
    args = [str(venv_python)] + sys.argv
    
    if sys.platform == "win32":
        subprocess.run(args, env=env, check=True)
        sys.exit(0)
    else:
        # 在 Linux/Mac 上使用 execv 替换当前进程，更干净
        os.execv(str(venv_python), args)
