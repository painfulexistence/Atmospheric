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

# 部署目的地：Project Cloudscape (editor) 的 repo 根目錄
# 例如 /Users/me/Desktop/code/project-cloudscape
# 解析優先序：--path= 旗標 > ATMOSPHERIC_EDITOR_PATH 環境變數
DEPLOY_DEST="${ATMOSPHERIC_EDITOR_PATH}"

# 解析參數
for arg in "$@"; do
    case "$arg" in
        --path=*)
            DEPLOY_DEST="${arg#*=}"
            ;;
    esac
done

SRC_PKG="${PROJECT_ROOT}/build-wasm/release/AtmosWasm"

echo -e "${BLUE}===================================================${NC}"
echo -e "${YELLOW}🚀 Atmospheric Engine - deployEditor 部署指令碼${NC}"
echo -e "${BLUE}===================================================${NC}"
echo -e "本地建置 package 目錄: ${GREEN}${SRC_PKG}${NC}"
if [ -n "${DEPLOY_DEST}" ]; then
    echo -e "部署目標 editor repo: ${GREEN}${DEPLOY_DEST}${NC}"
else
    echo -e "${RED}❌ 未指定部署目標。請使用 --path=<editor-repo> 或設定 ATMOSPHERIC_EDITOR_PATH${NC}"
    echo -e "   範例: ${YELLOW}./scripts/deployEditor.sh --path=/path/to/project-cloudscape${NC}"
    exit 1
fi
echo -e ""

# 1. 確認 editor repo 存在 (build 完才發現會白費時間)
if [ ! -d "${DEPLOY_DEST}" ]; then
    echo -e "${RED}❌ Editor repo 路徑不存在: ${DEPLOY_DEST}${NC}"
    exit 1
fi

# 2. 啟動 buildWasm release (使用 --no-server 略過伺服器啟動；
#    使用 --no-examples 只 build engine package，跳過所有 example targets)
echo -e ""
echo -e "${YELLOW}🔨 正在執行 Release WebAssembly 構建 (僅 Engine package)...${NC}"
"${SCRIPT_DIR}/buildWasm.sh" release --no-server --no-examples

# 3. 確認 build 產物完整
if [ ! -f "${SRC_PKG}/atmos.js" ] || [ ! -f "${SRC_PKG}/atmos.wasm" ]; then
    echo -e "${RED}❌ ${SRC_PKG} 內缺少 atmos.js 或 atmos.wasm — build 似乎不完整${NC}"
    exit 1
fi

# 4. 互動式環境要求確認 (CI 跳過)
DEST_PKG="${DEPLOY_DEST}/atmospheric"
echo -e ""
echo -e "${YELLOW}⚠️ 準備同步至: ${GREEN}${DEST_PKG}${NC}"
if [ -t 0 ]; then
    read -p "確定要將 build-wasm/release/AtmosWasm/ 內容同步/覆蓋至該目錄嗎？(y/N): " CONFIRM
    if [[ ! "$CONFIRM" =~ ^[yY]$ ]]; then
        echo -e "${RED}❌ 部署已取消。${NC}"
        exit 0
    fi
else
    echo -e "${BLUE}偵測到非互動式環境，自動進行部署...${NC}"
fi

# 5. 同步 (--delete 確保舊檔不殘留)
mkdir -p "${DEST_PKG}"
rsync -av --delete "${SRC_PKG}/" "${DEST_PKG}/"

# 6. 寫入 git commit hash 和編譯時間到 package.json 作為元資料
GIT_COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
if ! git diff --quiet 2>/dev/null; then
    GIT_COMMIT="${GIT_COMMIT}-dirty"
fi
BUILT_AT=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

node -e "
const fs = require('fs');
const pkgPath = '${DEST_PKG}/package.json';
if (fs.existsSync(pkgPath)) {
  const pkg = JSON.parse(fs.readFileSync(pkgPath, 'utf8'));
  pkg.atmospheric = {
    commitHash: '${GIT_COMMIT}',
    builtAt: '${BUILT_AT}'
  };
  delete pkg.commit;
  delete pkg.builtAt;
  fs.writeFileSync(pkgPath, JSON.stringify(pkg, null, 2) + '\n');
  console.log('   Metadata injected into package.json (atmospheric.commitHash: ${GIT_COMMIT})');
}
"

echo -e ""
echo -e "${GREEN}✨ 部署成功！${NC}"
echo -e "Editor 端可透過 ${YELLOW}\"@atmospheric/engine\": \"file:./atmospheric\"${NC} 引用。"
echo -e "${BLUE}===================================================${NC}"
