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

# 預設部署目的地 (可以是本地路徑如 /var/www/atmospheric 或遠端路徑如 user@vps:/var/www/atmospheric)
# 如果沒有特別指定，就只會更新本地專案根目錄下的 www 資料夾
DEPLOY_DEST="${ATMOSPHERIC_WWW_PATH}"

# 額外傳遞給 buildWasm.sh 的建置旗標 (例如 --webgpu)
BUILD_FLAGS=()

# 解析參數
for arg in "$@"; do
    case "$arg" in
        --path=*)
            DEPLOY_DEST="${arg#*=}"
            ;;
        --webgpu)
            # 透傳給 buildWasm.sh：以 WebGPU (而非 WebGL2) 建置範例
            BUILD_FLAGS+=("--webgpu")
            ;;
    esac
done

LOCAL_WWW="${PROJECT_ROOT}/www"

echo -e "${BLUE}===================================================${NC}"
echo -e "${YELLOW}🚀 Atmospheric Engine - deployWWW 部署指令碼${NC}"
echo -e "${BLUE}===================================================${NC}"
echo -e "本地建置網頁目錄 (www): ${GREEN}${LOCAL_WWW}${NC}"
if [ -n "${DEPLOY_DEST}" ]; then
    echo -e "部署目標路徑: ${GREEN}${DEPLOY_DEST}${NC}"
fi
echo -e ""

# 1. 初始化本地 www/ 目錄
echo -e "${YELLOW}📁 正在初始化本地 www/ 目錄...${NC}"
rm -rf "${LOCAL_WWW}"
mkdir -p "${LOCAL_WWW}/demo"

# 2. 部署 docs/ 下的內容至 www/ (僅限已 Commit 的檔案)
echo -e "${YELLOW}📁 正在同步 docs/ (僅限已 Commit 的檔案)...${NC}"
if [ -d "${PROJECT_ROOT}/docs" ]; then
    # 使用 git archive 提取 HEAD 中的 docs/ 目錄，並透過 tar 解開到 www/（去除首層 docs/ 資料夾前綴）
    git archive --format=tar HEAD docs | tar -x -C "${LOCAL_WWW}" --strip-components=1
    echo -e "${GREEN}✓ docs 同步成功！${NC}"
else
    echo -e "${RED}⚠️ 警告: 找不到 ${PROJECT_ROOT}/docs 目錄${NC}"
fi

# 3. 啟動 buildWasm release (使用 --no-server 略過伺服器啟動)
echo -e ""
if [ ${#BUILD_FLAGS[@]} -gt 0 ]; then
    echo -e "${YELLOW}🔨 正在執行 Release WebAssembly 構建 (${BUILD_FLAGS[*]})...${NC}"
else
    echo -e "${YELLOW}🔨 正在執行 Release WebAssembly 構建...${NC}"
fi
"${SCRIPT_DIR}/buildWasm.sh" release --no-server "${BUILD_FLAGS[@]}"

# 4. 尋找每個範例產物並複製到 www/demo/ 下，重新命名為 index.html
echo -e ""
echo -e "${YELLOW}📦 正在將 WebAssembly 範例拷貝至 www/demo/...${NC}"

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
            
            echo -e "  👉 整理範例: ${GREEN}$target_name${NC}..."
            
            # 複製整個資料夾到 www/demo/
            cp -R "$target_dir" "${LOCAL_WWW}/demo/${target_name}"
            
            # 將 <TargetName>.html 改名為 index.html (相容舊版與 OUTPUT_NAME 'index' 的新版產物)
            if [ -f "${LOCAL_WWW}/demo/${target_name}/${target_name}.html" ]; then
                mv "${LOCAL_WWW}/demo/${target_name}/${target_name}.html" "${LOCAL_WWW}/demo/${target_name}/index.html"
            fi
            
            COPIED_COUNT=$((COPIED_COUNT + 1))
        fi
    done
fi
# 4.5. 清除不必要的系統檔案及未追蹤的資源檔案
echo -e "${YELLOW}🧹 正在清理本地 www/ 目錄中的系統垃圾檔案 (.DS_Store) 與未追蹤的資源檔案...${NC}"

# 清除所有 .DS_Store
find "${LOCAL_WWW}" -name ".DS_Store" -type f -delete 2>/dev/null || true

# 取得 examples 資料夾到部署名稱的對照
get_deploy_name() {
    local folder_name="$1"
    case "$folder_name" in
        "LuaScripting") echo "AtmosLua" ;;
        "Physics2D") echo "Physics2DDemo" ;;
        "MazeFPS") echo "Maze" ;;
        *) echo "$folder_name" ;;
    esac
}

# 取得 git 中未追蹤的檔案清單，並從部署目錄中刪除
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    git ls-files --others --exclude-standard | while read -r untracked_file; do
        if [[ "$untracked_file" == Engine/default_assets/* ]]; then
            rel_path="${untracked_file#Engine/default_assets/}"
            rm -rf "${LOCAL_WWW}/demo"/*/assets/"$rel_path"
        elif [[ "$untracked_file" == Examples/* ]]; then
            if [[ "$untracked_file" == *"/assets/"* ]]; then
                sub_path="${untracked_file#Examples/}"
                folder_name="${sub_path%%/*}"
                asset_rel_path="${sub_path#*/}"
                deploy_name=$(get_deploy_name "$folder_name")
                rm -rf "${LOCAL_WWW}/demo/${deploy_name}/${asset_rel_path}"
            fi
        fi
    done
fi

echo -e "${GREEN}✓ 本地 www/ 目錄更新完成！(已整理 $COPIED_COUNT 個範例)${NC}"

# 5. 如果指定了部署目的地 (DEPLOY_DEST)，執行複製/同步手續
if [ -n "${DEPLOY_DEST}" ] && [ "${DEPLOY_DEST}" != "${LOCAL_WWW}" ]; then
    echo -e ""
    echo -e "${YELLOW}⚠️ 警告: 準備部署至目標目錄: ${GREEN}${DEPLOY_DEST}${NC}"
    
    # 僅在互動式終端機環境中進行手動確認，避免破壞 CI/CD 自動化流程
    if [ -t 0 ]; then
        read -p "確定要將 www/ 內容同步/覆蓋至該目錄嗎？這將會覆蓋舊檔案！(y/N): " CONFIRM
        if [[ ! "$CONFIRM" =~ ^[yY]$ ]]; then
            echo -e "${RED}❌ 部署已取消。${NC}"
            exit 0
        fi
    else
        echo -e "${BLUE}偵測到非互動式環境，自動進行部署...${NC}"
    fi

    if [[ "$DEPLOY_DEST" == *":"* ]]; then
        # 遠端複製 (例如 user@vps:/var/www/atmospheric)
        echo -e "${YELLOW}🚀 偵測到遠端路徑，正在將 www/ 同步上傳至遠端 ${DEPLOY_DEST}...${NC}"
        # 使用 rsync 進行高效的增量同步
        rsync -avz --delete "${LOCAL_WWW}/" "${DEPLOY_DEST}/"
        echo -e "${GREEN}✨ 遠端部署成功！${NC}"
    else
        # 本地複製 (例如 /var/www/atmospheric)
        echo -e "${YELLOW}📂 正在將 www/ 同步至本地目錄 ${DEPLOY_DEST}...${NC}"
        mkdir -p "${DEPLOY_DEST}"
        rsync -av --delete "${LOCAL_WWW}/" "${DEPLOY_DEST}/"
        echo -e "${GREEN}✨ 本地部署成功！${NC}"
    fi
fi

echo -e ""
echo -e "${GREEN}✨ 部署指令碼執行完畢！${NC}"
echo -e "${BLUE}===================================================${NC}"
