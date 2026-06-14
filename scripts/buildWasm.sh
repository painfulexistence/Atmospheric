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
    esac
done

echo -e "建置類型: ${GREEN}${BUILD_TYPE}${NC}"
echo -e "WebGPU 支援: ${GREEN}${WEBGPU_SUPPORT}${NC}"
echo -e ""

# 1. 檢查是否已設定 Emscripten SDK 環境變數
REQUIRED_VERSION=$("$SCRIPT_DIR/check-emscripten-version.sh" --print-primary-version)
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
"$SCRIPT_DIR/check-emscripten-version.sh"

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

# 4. 執行 CMake 設定
echo -e "${YELLOW}🛠️  正在為 Emscripten (WebAssembly) 設定 CMake 專案...${NC}"
emcmake cmake -G Ninja \
  -B "$BUILD_DIR" \
  -S . \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_OVERLAY_TRIPLETS="$(pwd)/triplets" \
  -DVCPKG_TARGET_TRIPLET=wasm32-emscripten \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DAE_USE_WEBGPU="$WEBGPU_SUPPORT"

# 5. 進行建置 (目標包含 AtmosLua, HelloWorld 與 Maze 迷宮遊戲)
echo -e ""
echo -e "${YELLOW}🔨 正在使用 Emscripten 建置所有 WebAssembly 目標...${NC}"
cmake --build "$BUILD_DIR" --parallel

echo -e ""
echo -e "${GREEN}✨ WebAssembly / Emscripten 建置成功！(${BUILD_TYPE})${NC}"
echo -e "網頁版產物已輸出至："
echo -e "  - AtmosLua:      ${YELLOW}$BUILD_DIR/AtmosLua/${NC}"
echo -e "  - HelloWorld:    ${YELLOW}$BUILD_DIR/HelloWorld/${NC}"
echo -e "  - Maze 迷宮:     ${YELLOW}$BUILD_DIR/Maze/${NC}"
echo -e "  - Physics2D 物理:${YELLOW}$BUILD_DIR/Physics2DDemo/${NC}"
echo -e "  - CSBDemo 角色:  ${YELLOW}$BUILD_DIR/CSBDemo/${NC}"
echo -e "  - VoxelWorld:    ${YELLOW}$BUILD_DIR/VoxelWorld/${NC}"
echo -e ""

# 6. 提供啟動本地伺服器的選項以便立即測試
echo -e "${BLUE}===================================================${NC}"
echo -e "${YELLOW}是否要在本地開啟 Web 伺服器以測試瀏覽器運行效果？(y/n)${NC}"
read -r RUN_SERVER

if [ "$RUN_SERVER" = "y" ] || [ "$RUN_SERVER" = "Y" ]; then
    PORT=8000
    echo -e ""
    echo -e "${GREEN}正在以建置輸出目錄為根目錄啟動 HTTP 伺服器...${NC}"
    echo -e "您可以透過以下連結存取各個網頁版目標："
    echo -e "  👉 AtmosLua (Lua 前端): ${BLUE}http://localhost:$PORT/AtmosLua/AtmosLua.html${NC}"
    echo -e "  👉 HelloWorld 範例:    ${BLUE}http://localhost:$PORT/HelloWorld/HelloWorld.html${NC}"
    echo -e "  👉 Maze 迷宮大作:      ${BLUE}http://localhost:$PORT/Maze/Maze.html${NC}"
    echo -e "  👉 Physics2D 物理範例:  ${BLUE}http://localhost:$PORT/Physics2DDemo/Physics2DDemo.html${NC}"
    echo -e "  👉 CSBDemo 角色範例:    ${BLUE}http://localhost:$PORT/CSBDemo/CSBDemo.html${NC}"
    echo -e "  👉 VoxelWorld 體素範例:  ${BLUE}http://localhost:$PORT/VoxelWorld/VoxelWorld.html${NC}"
    echo -e ""
    echo -e "按下 ${RED}Ctrl+C${NC} 可以停止伺服器。"
    echo -e ""

    # 如果是 Mac，自動幫忙在瀏覽器中開啟 Maze 迷宮遊戲
    if [[ "$OSTYPE" == "darwin"* ]]; then
        sleep 1 && open "http://localhost:$PORT/Maze/Maze.html" &
    fi

    # 使用 emrun 啟動伺服器（自動設定 COOP/COEP headers，支援 SharedArrayBuffer）
    emrun --no_browser --port $PORT "$BUILD_DIR"
    # 備案：若 emrun 不可用，改用以下指令（需要 Node.js）：
    # npx serve "$BUILD_DIR" --listen $PORT --config "$SCRIPT_DIR/serve.json"
fi
