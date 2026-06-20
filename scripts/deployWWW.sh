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

# 預設部署路徑，可透過環境變數 ATMOSPHERIC_WWW_PATH 覆蓋，或是使用參數 --path=
TARGET_DIR="${ATMOSPHERIC_WWW_PATH:-/var/www/atmospheric}"

# 解析參數
for arg in "$@"; do
    case "$arg" in
        --path=*)
            TARGET_DIR="${arg#*=}"
            ;;
    esac
done

DEMO_DIR="${TARGET_DIR}/demo"

echo -e "${BLUE}===================================================${NC}"
echo -e "${YELLOW}🚀 Atmospheric Engine - deployWWW 部署指令碼${NC}"
echo -e "${BLUE}===================================================${NC}"
echo -e "部署根目錄: ${GREEN}${TARGET_DIR}${NC}"
echo -e "範例部署目錄: ${GREEN}${DEMO_DIR}${NC}"
echo -e ""

# 1. 檢查並建立目錄
if [ ! -d "${TARGET_DIR}" ]; then
    echo -e "正在建立部署目錄 ${TARGET_DIR}..."
    mkdir -p "${TARGET_DIR}" || {
        echo -e "${RED}❌ 錯誤: 無法建立目錄 ${TARGET_DIR}。請檢查權限或使用 sudo 執行此指令碼。${NC}"
        exit 1
    }
fi

if [ ! -d "${DEMO_DIR}" ]; then
    echo -e "正在建立範例目錄 ${DEMO_DIR}..."
    mkdir -p "${DEMO_DIR}" || {
        echo -e "${RED}❌ 錯誤: 無法建立目錄 ${DEMO_DIR}。${NC}"
        exit 1
    }
fi

# 2. 部署 docs/ 下的內容至 TARGET_DIR
echo -e "${YELLOW}📁 正在部署 docs/ 內容至 ${TARGET_DIR}...${NC}"
if [ -d "${PROJECT_ROOT}/docs" ]; then
    cp -R "${PROJECT_ROOT}/docs/"* "${TARGET_DIR}/"
    echo -e "${GREEN}✓ docs 部署成功！${NC}"
else
    echo -e "${RED}⚠️ 警告: 找不到 ${PROJECT_ROOT}/docs 目綠${NC}"
fi

# 3. 啟動 buildWasm release (使用 --no-server 略過伺服器啟動)
echo -e ""
echo -e "${YELLOW}🔨 正在執行 Release WebAssembly 構建...${NC}"
"${SCRIPT_DIR}/buildWasm.sh" release --no-server

# 4. 尋找每個範例產物並複製到 demo 目錄，重新命名為 index.html
echo -e ""
echo -e "${YELLOW}📦 正在將 WebAssembly 範例部署至 ${DEMO_DIR}...${NC}"

RELEASE_DIR="${PROJECT_ROOT}/build-wasm/release"
COPIED_COUNT=0

if [ -d "${RELEASE_DIR}" ]; then
    for target_dir in "${RELEASE_DIR}"/*/; do
        target_dir=${target_dir%/}
        
        # 檢查該資料夾下是否有任何 .html 檔案
        if ls "$target_dir"/*.html >/dev/null 2>&1; then
            target_name=$(basename "$target_dir")
            
            # 排除內建編譯系統資料夾
            if [ "$target_name" = "CMakeFiles" ] || [ "$target_name" = "vcpkg_installed" ] || [ "$target_name" = "lib" ]; then
                continue
            fi
            
            echo -e "  👉 部署範例: ${GREEN}$target_name${NC}..."
            
            # 清除舊有的部署目錄
            rm -rf "${DEMO_DIR}/${target_name}"
            
            # 複製整個資料夾到 demo/
            cp -R "$target_dir" "${DEMO_DIR}/${target_name}"
            
            # 將 <TargetName>.html 改名為 index.html
            if [ -f "${DEMO_DIR}/${target_name}/${target_name}.html" ]; then
                mv "${DEMO_DIR}/${target_name}/${target_name}.html" "${DEMO_DIR}/${target_name}/index.html"
            fi
            
            COPIED_COUNT=$((COPIED_COUNT + 1))
        fi
    done
fi

echo -e ""
echo -e "${GREEN}✨ 成功部署 $COPIED_COUNT 個 WebAssembly 範例至 ${DEMO_DIR}！${NC}"
echo -e "${BLUE}===================================================${NC}"
