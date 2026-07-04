#!/bin/bash
set -e

# 顏色定義
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # 無顏色

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo -e "${BLUE}===================================================${NC}"
echo -e "${YELLOW}🎮 Atmospheric Engine - WebAssembly 建置系統${NC}"
echo -e "${BLUE}===================================================${NC}"

# 預設建置類型為 Release
BUILD_TYPE="Release"
WEBGPU_SUPPORT="OFF"
NO_SERVER="OFF"
NO_EXAMPLES="OFF"
CLEAN_BUILD="OFF"

# 解析參數
for arg in "$@"; do
    case "$arg" in
        debug|Debug)
            BUILD_TYPE="Debug"
            ;;
        release|Release)
            BUILD_TYPE="Release"
            ;;
        --webgpu)
            WEBGPU_SUPPORT="ON"
            ;;
        --no-server)
            NO_SERVER="ON"
            ;;
        --no-examples)
            NO_EXAMPLES="ON"
            ;;
        --clean)
            CLEAN_BUILD="ON"
            ;;
    esac
done

echo -e "建置類型: ${GREEN}${BUILD_TYPE}${NC}"
echo -e "WebGPU 支援: ${GREEN}${WEBGPU_SUPPORT}${NC}"
if [ "$NO_EXAMPLES" = "ON" ]; then
    echo -e "Examples: ${YELLOW}略過${NC}"
fi
echo -e ""

# 1. 檢查是否已設定 Emscripten SDK 環境變數
REQUIRED_VERSION=$("$SCRIPT_DIR/checkEmscriptenVersion.sh" --print-primary-version)
if [ -z "$EMSDK" ]; then
    echo -e "${RED}❌ 錯誤: 未偵測到 \$EMSDK 環境變數。${NC}"
    echo -e "要為 WebAssembly (Emscripten) 進行建置，您必須先安裝並啟用 Emscripten SDK (emsdk)。"
    echo -e ""
    echo -e "${YELLOW}💡 如何安裝與啟用 EMSDK:${NC}"
    echo -e "  1. 複製 emsdk 倉庫: ${GREEN}git clone https://github.com/emscripten-core/emsdk.git${NC}"
    echo -e "  2. 進入目錄: ${GREEN}cd emsdk${NC}"
    echo -e "  3. 安裝最新版工具: ${GREEN}./emsdk install ${REQUIRED_VERSION}${NC}"
    echo -e "  4. 啟用最新版工具: ${GREEN}./emsdk activate ${REQUIRED_VERSION}${NC}"
    echo -e "  5. 載入環境變數: ${GREEN}source ./emsdk_env.sh${NC}"
    echo -e ""
    echo -e "載入環境變數後，請在${YELLOW}同一個終端機視窗${NC}中重新執行此指令碼。"
    exit 1
fi

source "$EMSDK/emsdk_env.sh" > /dev/null 2>&1
"$SCRIPT_DIR/checkEmscriptenVersion.sh"

echo -e "${GREEN}✓ 找到 EMSDK 路徑: $EMSDK${NC}"
echo -e "Emscripten 編譯器版本: $(emcc --version | head -n 1)"

# 2. 檢查 vcpkg 子模組
VCPKG_DIR="$(pwd)/vcpkg"
if [ ! -d "$VCPKG_DIR" ] || [ ! -f "$VCPKG_DIR/vcpkg" ]; then
    echo -e "${YELLOW}⚠️ 未找到 vcpkg 或尚未初始化，正在更新子模組...${NC}"
    git submodule update --init --recursive
    if [ -f "$VCPKG_DIR/bootstrap-vcpkg.sh" ]; then
        echo -e "${YELLOW}正在引導 (Bootstrap) vcpkg...${NC}"
        "$VCPKG_DIR/bootstrap-vcpkg.sh" -disableMetrics
    fi
fi

# 3. 定義 WebAssembly 專屬建置目錄
BUILD_SUBDIR=$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')
BUILD_DIR="$(pwd)/build-wasm/$BUILD_SUBDIR"
echo -e "${BLUE}配置資訊:${NC}"
echo -e "  - 專案根目錄: $(pwd)"
echo -e "  - 建置目錄:   $BUILD_DIR"
echo -e ""

# 3.5 Clean build: 移除整個建置目錄，確保輸出可重現、不殘留已改名/移除範例的舊產物。
# (vcpkg_installed 也會一併移除，但重新設定時會從 vcpkg 的 binary cache 快速還原，
#  而非從原始碼重建依賴。)
if [ "$CLEAN_BUILD" = "ON" ] && [ -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}🧹 Clean build: 正在移除既有建置目錄 $BUILD_DIR ...${NC}"
    rm -rf "$BUILD_DIR"
    echo -e ""
fi

# 4. 執行 CMake 設定
echo -e "${YELLOW}🛠️  正在為 Emscripten (WebAssembly) 設定 CMake 專案...${NC}"
BUILD_EXAMPLES_FLAG="ON"
if [ "$NO_EXAMPLES" = "ON" ]; then
    BUILD_EXAMPLES_FLAG="OFF"
fi
emcmake cmake -G Ninja \
  -B "$BUILD_DIR" \
  -S . \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_OVERLAY_TRIPLETS="$(pwd)/triplets" \
  -DVCPKG_TARGET_TRIPLET=wasm32-emscripten \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DAE_USE_WEBGPU="$WEBGPU_SUPPORT" \
  -DAE_BUILD_EXAMPLES="$BUILD_EXAMPLES_FLAG"

# 5. 進行建置 (自動偵測記憶體以避免 OOM)
PARALLEL_JOBS="--parallel"
TOTAL_MEM_MB=""

if [ -f /proc/meminfo ]; then
    # Linux 系統偵測
    TOTAL_MEM_KB=$(grep MemTotal /proc/meminfo | awk '{print $2}')
    TOTAL_MEM_MB=$((TOTAL_MEM_KB / 1024))
elif [ "$(uname)" = "Darwin" ]; then
    # macOS 系統偵測
    TOTAL_MEM_BYTES=$(sysctl -n hw.memsize)
    TOTAL_MEM_MB=$((TOTAL_MEM_BYTES / 1024 / 1024))
fi

if [ -n "$TOTAL_MEM_MB" ]; then
    # 記憶體小於 2GB -> 單執行緒編譯；小於 4GB -> 雙執行緒編譯
    if [ "$TOTAL_MEM_MB" -lt 2048 ]; then
        PARALLEL_JOBS="-j 1"
        echo -e "${YELLOW}⚠️ 偵測到系統記憶體偏低 (${TOTAL_MEM_MB}MB)，限制編譯並行數為 1 (單執行緒)，以防止編譯器被系統強制終止 (OOM Killed)。${NC}"
    elif [ "$TOTAL_MEM_MB" -lt 4096 ]; then
        PARALLEL_JOBS="-j 2"
        echo -e "${YELLOW}⚠️ 偵測到系統記憶體偏低 (${TOTAL_MEM_MB}MB)，限制編譯並行數為 2，以防止記憶體耗盡。${NC}"
    fi
fi

echo -e ""
echo -e "${YELLOW}🔨 正在使用 Emscripten 建置所有 WebAssembly 目標...${NC}"
cmake --build "$BUILD_DIR" $PARALLEL_JOBS

echo -e ""
echo -e "${GREEN}✨ WebAssembly / Emscripten 建置成功！(${BUILD_TYPE})${NC}"
echo -e "網頁版產物已輸出至：${YELLOW}$BUILD_DIR/${NC}"
echo -e ""

# 6. 提供啟動本地伺服器的選項以便立即測試
if [ "$NO_SERVER" = "ON" ] || [ ! -t 0 ]; then
    echo -e "${YELLOW}偵測到非互動式環境或已設定 --no-server，略過本地伺服器啟動。${NC}"
    exit 0
fi

echo -e "${BLUE}===================================================${NC}"
echo -e "${YELLOW}是否要在本地開啟 Web 伺服器以測試瀏覽器運行效果？(y/n)${NC}"
read -r RUN_SERVER

if [ "$RUN_SERVER" = "y" ] || [ "$RUN_SERVER" = "Y" ]; then
    PORT=8000
    echo -e ""
    echo -e "${GREEN}正在以建置輸出目錄為根目錄啟動 HTTP 伺服器...${NC}"
    echo -e "您可以透過以下連結存取各個網頁版目標："
    for target_dir in "$BUILD_DIR"/*/; do
        # 移除結尾的斜線
        target_dir=${target_dir%/}
        target_name=$(basename "$target_dir")

        # 排除系統/輔助目錄
        if [ "$target_name" = "CMakeFiles" ] || [ "$target_name" = "vcpkg_installed" ] || [ "$target_name" = "lib" ] || [ "$target_name" = "bin" ]; then
            continue
        fi

        # 尋找該目錄下的首個 .html 檔案
        html_file=$(find "$target_dir" -maxdepth 1 -name "*.html" | head -n 1)
        if [ -n "$html_file" ]; then
            html_name=$(basename "$html_file")
            if [ "$html_name" = "index.html" ]; then
                echo -e "  👉 ${GREEN}$target_name${NC}: ${BLUE}http://localhost:$PORT/$target_name/${NC}"
            else
                echo -e "  👉 ${GREEN}$target_name${NC}: ${BLUE}http://localhost:$PORT/$target_name/$html_name${NC}"
            fi
        fi
    done
    echo -e ""
    echo -e "按下 ${RED}Ctrl+C${NC} 可以停止伺服器。"
    echo -e ""

    # 如果是 Mac，自動在瀏覽器中開啟 3DBasics 範例
    if [[ "$OSTYPE" == "darwin"* ]]; then
        hw_dir="$BUILD_DIR/3DBasics"
        if [ -d "$hw_dir" ]; then
            html_file=$(find "$hw_dir" -maxdepth 1 -name "*.html" | head -n 1)
            if [ -n "$html_file" ]; then
                html_name=$(basename "$html_file")
                if [ "$html_name" = "index.html" ]; then
                    sleep 1 && open "http://localhost:$PORT/3DBasics/" &
                else
                    sleep 1 && open "http://localhost:$PORT/3DBasics/$html_name" &
                fi
            else
                sleep 1 && open "http://localhost:$PORT/" &
            fi
        else
            sleep 1 && open "http://localhost:$PORT/" &
        fi
    fi

    # 使用 emrun 啟動伺服器（自動設定 COOP/COEP headers，支援 SharedArrayBuffer）
    emrun --no_browser --port $PORT "$BUILD_DIR"
fi
