#!/bin/bash
set -e

# 顏色定義
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # 無顏色

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo -e "${BLUE}===================================================${NC}"
echo -e "${YELLOW}🚀 正在部署 WebAssembly Release 產物至 www/ 目錄...${NC}"
echo -e "${BLUE}===================================================${NC}"

RELEASE_DIR="$PROJECT_ROOT/build-wasm/release"
DEPLOY_DIR="$PROJECT_ROOT/www"

# 1. 檢查 Release 建置目錄是否存在
if [ ! -d "$RELEASE_DIR" ]; then
    echo -e "${RED}❌ 錯誤: 找不到 Release 建置目錄 $RELEASE_DIR${NC}"
    echo -e "請先執行 Release 建置：${GREEN}./scripts/buildWasmRelease.sh${NC}"
    exit 1
fi

# 2. 如果 www/ 目錄不存在則建立之
if [ ! -d "$DEPLOY_DIR" ]; then
    echo -e "${YELLOW}正在建立部署目錄 $DEPLOY_DIR...${NC}"
    mkdir -p "$DEPLOY_DIR"
fi

# 3. 尋找並複製需要部署的目標資料夾
# 我們會尋找 build-wasm/release 底下含有 .html 檔案的直接子目錄（如 AtmosLua, HelloWorld, Maze）
COPIED_COUNT=0

for target_dir in "$RELEASE_DIR"/*/; do
    # 移除結尾的斜線
    target_dir=${target_dir%/}
    
    # 檢查該資料夾下是否有任何 .html 檔案
    if ls "$target_dir"/*.html >/dev/null 2>&1; then
        target_name=$(basename "$target_dir")
        
        # 排除內建編譯系統資料夾
        if [ "$target_name" = "CMakeFiles" ] || [ "$target_name" = "vcpkg_installed" ] || [ "$target_name" = "lib" ]; then
            continue
        fi
        
        echo -e "正在部署目標：${GREEN}$target_name${NC}..."
        
        # 清除舊有的部署目錄以避免殘留舊檔案
        rm -rf "$DEPLOY_DIR/$target_name"
        
        # 複製整個資料夾到 www/
        cp -R "$target_dir" "$DEPLOY_DIR/$target_name"
        COPIED_COUNT=$((COPIED_COUNT + 1))
    fi
done

echo -e ""
if [ $COPIED_COUNT -gt 0 ]; then
    echo -e "${GREEN}✨ 成功部署 $COPIED_COUNT 個 WebAssembly 目標至 $DEPLOY_DIR！${NC}"
    echo -e "您現在可以在 ${YELLOW}www/${NC} 目錄中啟動網頁伺服器（如 python -m http.server 或 serve）來進行測試。"
else
    echo -e "${RED}⚠️ 在 $RELEASE_DIR 底下找不到任何含有 .html 檔的部署目標。${NC}"
fi
echo -e "${BLUE}===================================================${NC}"
