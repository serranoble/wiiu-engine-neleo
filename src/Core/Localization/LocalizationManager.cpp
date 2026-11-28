#include "Core/Localization/LocalizationManager.hpp"
#include "Core/Localization/BuiltinCatalogs.inl"
#include "Core/AssetManager.hpp"
#include "Core/Storage/VirtualFileSystem.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Neleo {

namespace {

[[nodiscard]] std::string_view TrimView(std::string_view sv) noexcept {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
        sv.remove_prefix(1u);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
        sv.remove_suffix(1u);
    }
    return sv;
}

void CopyTruncated(char* dst, size_t dstSize, std::string_view src) noexcept {
    if (!dst || dstSize == 0u) {
        return;
    }
    const size_t copyLen = std::min(src.size(), dstSize - 1u);
    if (copyLen > 0u) {
        std::memcpy(dst, src.data(), copyLen);
    }
    dst[copyLen] = '\0';
}

[[nodiscard]] bool EqualsIgnoreCaseAscii(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0u; i < a.size(); ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool HasAnyValidEntry(std::string_view content) noexcept {
    if (content.size() >= 3u &&
        static_cast<unsigned char>(content[0]) == 0xEFu &&
        static_cast<unsigned char>(content[1]) == 0xBBu &&
        static_cast<unsigned char>(content[2]) == 0xBFu) {
        content.remove_prefix(3u);
    }
    size_t pos = 0u;
    while (pos < content.size()) {
        size_t end = content.find('\n', pos);
        if (end == std::string_view::npos) end = content.size();
        const std::string_view line = TrimView(content.substr(pos, end - pos));
        pos = (end < content.size()) ? (end + 1u) : content.size();
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        const size_t eq = line.find('=');
        if (eq != std::string_view::npos && eq > 0u && !TrimView(line.substr(0u, eq)).empty()) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::string_view GetBuiltinCatalogForCode(std::string_view code) noexcept {
    if (EqualsIgnoreCaseAscii(code, "es")) {
        return kBuiltinCatalogEs;
    }
    if (EqualsIgnoreCaseAscii(code, "en")) {
        return kBuiltinCatalogEn;
    }
    return {};
}

template <typename Fn>
bool WithVirtualTextFile(std::string_view virtualPath, Fn&& onContent) noexcept {
    if (virtualPath.empty()) {
        return false;
    }

    // 1. Ruta primaria canónica: AssetManager::LoadBinaryFile (probada en Cemu, Wii U real y Windows)
    static constexpr std::string_view kContentPrefix = "content:/";
    if (virtualPath.size() > kContentPrefix.size() &&
        virtualPath.substr(0u, kContentPrefix.size()) == kContentPrefix) {
        const std::string_view rel = virtualPath.substr(kContentPrefix.size());
        try {
            std::vector<uint8_t> bytes;
            if (AssetManager::LoadBinaryFile(std::string(rel), bytes) && !bytes.empty()) {
                return onContent(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            }
        } catch (...) {
        }
    }

    // 2. Ruta alternativa vía VirtualFileSystem (save:/ o content:/ alineado)
    VFSAlignedBuffer vfsBuf{};
    if (VirtualFileSystem::ReadFileAligned(virtualPath, vfsBuf, 64u, MemoryTag::General, "Loc_Lang")) {
        const bool ok = (vfsBuf.data != nullptr && vfsBuf.size > 0u)
            ? onContent(std::string_view(reinterpret_cast<const char*>(vfsBuf.data), vfsBuf.size))
            : false;
        VirtualFileSystem::FreeAlignedBuffer(vfsBuf);
        return ok;
    }
    return false;
}

} // namespace

// ============================================================================
// LocalizationTable
// ============================================================================

void LocalizationTable::Clear() noexcept {
    m_slots.fill(EntrySlot{});
    m_entryCount = 0u;
    m_textUsed = 0u;
    m_textBuffer[0] = '\0';
}

bool LocalizationTable::Insert(std::string_view key, std::string_view value) noexcept {
    if (key.empty()) {
        return false;
    }
    // Mantener factor de carga <= 75% para búsqueda O(1) acotada
    if (m_entryCount >= (HASH_TABLE_CAPACITY * 3u) / 4u) {
        return false;
    }
    if (value.size() > 0xFFFFu) {
        return false;
    }
    if (m_textUsed + value.size() + 1u > TEXT_BUFFER_CAPACITY) {
        return false;
    }

    const uint64_t hash = HashLocKey(key);
    constexpr size_t kMask = HASH_TABLE_CAPACITY - 1u;
    size_t idx = static_cast<size_t>(hash) & kMask;

    for (size_t probe = 0u; probe < HASH_TABLE_CAPACITY; ++probe) {
        EntrySlot& slot = m_slots[idx];
        if (slot.hash == 0ULL || slot.hash == hash) {
            const bool isNew = (slot.hash == 0ULL);
            const uint32_t offset = static_cast<uint32_t>(m_textUsed);
            if (!value.empty()) {
                std::memcpy(m_textBuffer.data() + offset, value.data(), value.size());
            }
            m_textBuffer[offset + value.size()] = '\0';
            m_textUsed += value.size() + 1u;

            slot.hash = hash;
            slot.offset = offset;
            slot.length = static_cast<uint16_t>(value.size());
            if (isNew) {
                ++m_entryCount;
            }
            return true;
        }
        idx = (idx + 1u) & kMask;
    }
    return false;
}

size_t LocalizationTable::ParseFromMemory(std::string_view content) noexcept {
    Clear();
    if (content.empty()) {
        return 0u;
    }

    // Saltar BOM UTF-8 si existiera
    if (content.size() >= 3u &&
        static_cast<unsigned char>(content[0]) == 0xEFu &&
        static_cast<unsigned char>(content[1]) == 0xBBu &&
        static_cast<unsigned char>(content[2]) == 0xBFu) {
        content.remove_prefix(3u);
    }

    size_t pos = 0u;
    while (pos < content.size()) {
        size_t end = content.find('\n', pos);
        if (end == std::string_view::npos) {
            end = content.size();
        }
        std::string_view line = TrimView(content.substr(pos, end - pos));
        pos = (end < content.size()) ? (end + 1u) : content.size();

        if (line.empty() || line.front() == '#' || line.front() == ';') {
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string_view::npos || eq == 0u) {
            continue;
        }

        const std::string_view key = TrimView(line.substr(0u, eq));
        std::string_view val = line.substr(eq + 1u);
        while (!val.empty() && (val.back() == '\r' || val.back() == '\n')) {
            val.remove_suffix(1u);
        }
        if (!val.empty() && (val.front() == ' ' || val.front() == '\t')) {
            val = TrimView(val);
        }

        (void)Insert(key, val);
    }

    return m_entryCount;
}

const char* LocalizationTable::FindCStr(uint64_t keyHash, size_t* outLength) const noexcept {
    if (keyHash == 0ULL || m_entryCount == 0u) {
        return nullptr;
    }

    constexpr size_t kMask = HASH_TABLE_CAPACITY - 1u;
    size_t idx = static_cast<size_t>(keyHash) & kMask;

    for (size_t probe = 0u; probe < HASH_TABLE_CAPACITY; ++probe) {
        const EntrySlot& slot = m_slots[idx];
        if (slot.hash == 0ULL) {
            return nullptr;
        }
        if (slot.hash == keyHash) {
            if (outLength) {
                *outLength = slot.length;
            }
            return m_textBuffer.data() + slot.offset;
        }
        idx = (idx + 1u) & kMask;
    }
    return nullptr;
}

std::string_view LocalizationTable::Find(uint64_t keyHash) const noexcept {
    size_t len = 0u;
    const char* ptr = FindCStr(keyHash, &len);
    if (!ptr) {
        return {};
    }
    return std::string_view(ptr, len);
}

// ============================================================================
// LocalizationManager
// ============================================================================

bool LocalizationManager::Initialize(std::string_view initialLanguage) noexcept {
    m_languageCount = 0u;
    m_activeLanguageIndex = 0u;
    m_fallbackTable.Clear();
    m_activeTable.Clear();
    m_activeIsFallback = true;

    if (!LoadLanguageManifest("content:/lang/languages.cfg") || m_languageCount == 0u) {
        (void)RegisterLanguage("es", "ESPANOL", "content:/lang/es.lang");
        (void)RegisterLanguage("en", "ENGLISH", "content:/lang/en.lang");
    }

    // Cargar tabla fallback base ("es") una sola vez (archivo externo con prioridad; respaldo embebido si falta)
    bool fbLoaded = false;
    if (const LanguageDescriptor* fbDesc = FindLanguageDescriptor(DEFAULT_FALLBACK_LANG)) {
        fbLoaded = LoadTableFromVirtualPath(fbDesc->virtualPath, m_fallbackTable);
    } else if (m_languageCount > 0u) {
        fbLoaded = LoadTableFromVirtualPath(m_languages[0].virtualPath, m_fallbackTable);
    }
    if (!fbLoaded) {
        const std::string_view builtinEs = GetBuiltinCatalogForCode(DEFAULT_FALLBACK_LANG);
        if (!builtinEs.empty()) {
            (void)m_fallbackTable.ParseFromMemory(builtinEs);
        }
    }

    m_initialized = true;

    if (!SetLanguage(initialLanguage)) {
        (void)SetLanguage(DEFAULT_FALLBACK_LANG);
    }
    return true;
}

void LocalizationManager::Shutdown() noexcept {
    m_fallbackTable.Clear();
    m_activeTable.Clear();
    m_languageCount = 0u;
    m_activeLanguageIndex = 0u;
    m_activeIsFallback = true;
    for (auto& slot : m_observers) {
        slot = ObserverSlot{};
    }
    m_initialized = false;
}

bool LocalizationManager::LoadLanguageManifest(std::string_view manifestVirtualPath) noexcept {
    size_t registered = 0u;
    const bool readOk = WithVirtualTextFile(manifestVirtualPath, [this, &registered](std::string_view content) noexcept {
        size_t pos = 0u;
        while (pos < content.size()) {
            size_t end = content.find('\n', pos);
            if (end == std::string_view::npos) {
                end = content.size();
            }
            std::string_view line = TrimView(content.substr(pos, end - pos));
            pos = (end < content.size()) ? (end + 1u) : content.size();

            if (line.empty() || line.front() == '#' || line.front() == ';') {
                continue;
            }

            const size_t p1 = line.find('|');
            if (p1 == std::string_view::npos) continue;
            const size_t p2 = line.find('|', p1 + 1u);
            if (p2 == std::string_view::npos) continue;

            const std::string_view code = TrimView(line.substr(0u, p1));
            const std::string_view name = TrimView(line.substr(p1 + 1u, p2 - p1 - 1u));
            const std::string_view path = TrimView(line.substr(p2 + 1u));

            if (RegisterLanguage(code, name, path)) {
                ++registered;
            }
        }
        return registered > 0u;
    });

    return readOk && registered > 0u;
}

bool LocalizationManager::RegisterLanguage(std::string_view code,
                                           std::string_view displayName,
                                           std::string_view virtualPath) noexcept {
    if (code.empty() || displayName.empty() || virtualPath.empty()) {
        return false;
    }

    // Actualizar si el código ya existe
    for (size_t i = 0u; i < m_languageCount; ++i) {
        if (EqualsIgnoreCaseAscii(m_languages[i].code, code)) {
            CopyTruncated(m_languages[i].displayName, sizeof(m_languages[i].displayName), displayName);
            CopyTruncated(m_languages[i].virtualPath, sizeof(m_languages[i].virtualPath), virtualPath);
            return true;
        }
    }

    if (m_languageCount >= MAX_LANGUAGES) {
        return false;
    }

    LanguageDescriptor& desc = m_languages[m_languageCount++];
    CopyTruncated(desc.code, sizeof(desc.code), code);
    CopyTruncated(desc.displayName, sizeof(desc.displayName), displayName);
    CopyTruncated(desc.virtualPath, sizeof(desc.virtualPath), virtualPath);
    return true;
}

const LanguageDescriptor* LocalizationManager::FindLanguageDescriptor(std::string_view code) const noexcept {
    for (size_t i = 0u; i < m_languageCount; ++i) {
        if (EqualsIgnoreCaseAscii(m_languages[i].code, code)) {
            return &m_languages[i];
        }
    }
    return nullptr;
}

bool LocalizationManager::HasLanguage(std::string_view code) const noexcept {
    return FindLanguageDescriptor(code) != nullptr;
}

bool LocalizationManager::LoadTableFromVirtualPath(std::string_view virtualPath, LocalizationTable& outTable) noexcept {
    return WithVirtualTextFile(virtualPath, [&outTable](std::string_view content) noexcept {
        // Validar antes de limpiar outTable para nunca destruir la tabla activa ante un archivo vacío/malformado
        if (!HasAnyValidEntry(content)) {
            return false;
        }
        return outTable.ParseFromMemory(content) > 0u;
    });
}

bool LocalizationManager::SetLanguage(std::string_view code) noexcept {
    if (m_languageCount == 0u || code.empty()) {
        return false;
    }

    size_t targetIdx = m_languageCount;
    for (size_t i = 0u; i < m_languageCount; ++i) {
        if (EqualsIgnoreCaseAscii(m_languages[i].code, code)) {
            targetIdx = i;
            break;
        }
    }
    if (targetIdx >= m_languageCount) {
        return false;
    }

    const LanguageDescriptor& desc = m_languages[targetIdx];
    const bool isFallback = EqualsIgnoreCaseAscii(desc.code, DEFAULT_FALLBACK_LANG);

    if (isFallback) {
        m_activeTable.Clear();
        m_activeIsFallback = true;
    } else {
        if (!LoadTableFromVirtualPath(desc.virtualPath, m_activeTable)) {
            const std::string_view builtin = GetBuiltinCatalogForCode(desc.code);
            if (builtin.empty() || m_activeTable.ParseFromMemory(builtin) == 0u) {
                return false;
            }
        }
        m_activeIsFallback = false;
    }

    m_activeLanguageIndex = targetIdx;
    NotifyObservers();
    return true;
}

bool LocalizationManager::CycleLanguage() noexcept {
    if (m_languageCount <= 1u) {
        return false;
    }
    const size_t nextIdx = (m_activeLanguageIndex + 1u) % m_languageCount;
    return SetLanguage(m_languages[nextIdx].code);
}

std::string_view LocalizationManager::GetCurrentLanguageCode() const noexcept {
    if (m_languageCount == 0u || m_activeLanguageIndex >= m_languageCount) {
        return DEFAULT_FALLBACK_LANG;
    }
    return m_languages[m_activeLanguageIndex].code;
}

std::string_view LocalizationManager::GetCurrentLanguageDisplayName() const noexcept {
    if (m_languageCount == 0u || m_activeLanguageIndex >= m_languageCount) {
        return "ESPANOL";
    }
    return m_languages[m_activeLanguageIndex].displayName;
}

bool LocalizationManager::IsCurrentLanguage(std::string_view code) const noexcept {
    return EqualsIgnoreCaseAscii(GetCurrentLanguageCode(), code);
}

const LanguageDescriptor* LocalizationManager::GetLanguageAt(size_t index) const noexcept {
    if (index >= m_languageCount) {
        return nullptr;
    }
    return &m_languages[index];
}

std::string_view LocalizationManager::Get(uint64_t keyHash, std::string_view fallback) const noexcept {
    size_t len = 0u;
    if (!m_activeIsFallback) {
        if (const char* found = m_activeTable.FindCStr(keyHash, &len)) {
            return std::string_view(found, len);
        }
    }
    if (const char* fb = m_fallbackTable.FindCStr(keyHash, &len)) {
        return std::string_view(fb, len);
    }
    return fallback;
}

std::string_view LocalizationManager::Get(std::string_view key) const noexcept {
    return Get(HashLocKey(key), key);
}

const char* LocalizationManager::GetCStr(uint64_t keyHash, const char* fallback) const noexcept {
    if (!m_activeIsFallback) {
        if (const char* found = m_activeTable.FindCStr(keyHash)) {
            return found;
        }
    }
    if (const char* fb = m_fallbackTable.FindCStr(keyHash)) {
        return fb;
    }
    return fallback ? fallback : "";
}

const char* LocalizationManager::GetCStr(const char* key) const noexcept {
    if (!key || key[0] == '\0') {
        return "";
    }
    return GetCStr(HashLocKey(std::string_view(key)), key);
}

uint32_t LocalizationManager::RegisterChangeCallback(LanguageChangeCallback cb, void* userData) noexcept {
    if (!cb) {
        return 0u;
    }
    for (auto& slot : m_observers) {
        if (slot.id == 0u) {
            slot.id = m_nextObserverId++;
            if (m_nextObserverId == 0u) {
                m_nextObserverId = 1u;
            }
            slot.callback = cb;
            slot.userData = userData;
            return slot.id;
        }
    }
    return 0u;
}

void LocalizationManager::UnregisterChangeCallback(uint32_t observerId) noexcept {
    if (observerId == 0u) {
        return;
    }
    for (auto& slot : m_observers) {
        if (slot.id == observerId) {
            slot = ObserverSlot{};
            return;
        }
    }
}

size_t LocalizationManager::GetActiveEntryCount() const noexcept {
    return m_activeIsFallback ? m_fallbackTable.GetEntryCount() : m_activeTable.GetEntryCount();
}

void LocalizationManager::NotifyObservers() noexcept {
    const std::string_view currentCode = GetCurrentLanguageCode();
    // Copia local en stack para seguridad ante re-entrancia si un observador se desregistra durante el callback
    const std::array<ObserverSlot, MAX_OBSERVERS> snapshot = m_observers;
    for (const auto& slot : snapshot) {
        if (slot.id != 0u && slot.callback != nullptr) {
            slot.callback(currentCode, slot.userData);
        }
    }
}

bool LocalizationManager::VerifyIntegrity(const char** outFailureReason) noexcept {
    auto fail = [outFailureReason](const char* reason) noexcept -> bool {
        if (outFailureReason) {
            *outFailureReason = reason;
        }
        return false;
    };

    // Instancia aislada en BSS (0 side-effects sobre el singleton vivo ni sus observadores)
    static LocalizationManager s_isolated;
    s_isolated.Shutdown();

    if (!s_isolated.Initialize("es")) {
        s_isolated.Shutdown();
        return fail("FAIL:INIT");
    }

    uint32_t observerCalls = 0u;
    const uint32_t obsId = s_isolated.RegisterChangeCallback(
        [](std::string_view /*code*/, void* ud) {
            if (auto* cnt = static_cast<uint32_t*>(ud)) {
                ++(*cnt);
            }
        },
        &observerCalls);

    if (!s_isolated.HasLanguage("es") || !s_isolated.HasLanguage("en") || s_isolated.GetLanguageCount() < 2u) {
        s_isolated.UnregisterChangeCallback(obsId);
        s_isolated.Shutdown();
        return fail("FAIL:LANG_COUNT");
    }
    if (!s_isolated.SetLanguage("en")) {
        s_isolated.UnregisterChangeCallback(obsId);
        s_isolated.Shutdown();
        return fail("FAIL:SET_EN");
    }
    if (s_isolated.Get(HashLocKeyLiteral("menu.item.options.label"), "") != "ENGINE OPTIONS" ||
        !s_isolated.IsCurrentLanguage("EN")) {
        s_isolated.UnregisterChangeCallback(obsId);
        s_isolated.Shutdown();
        return fail("FAIL:EN_MATCH");
    }
    if (!s_isolated.SetLanguage("es")) {
        s_isolated.UnregisterChangeCallback(obsId);
        s_isolated.Shutdown();
        return fail("FAIL:SET_ES");
    }
    if (s_isolated.Get(HashLocKeyLiteral("menu.item.options.label"), "") != "OPCIONES DEL MOTOR" ||
        !s_isolated.IsCurrentLanguage("ES")) {
        s_isolated.UnregisterChangeCallback(obsId);
        s_isolated.Shutdown();
        return fail("FAIL:ES_MATCH");
    }
    if (s_isolated.Get("loc.nonexistent.key") != "loc.nonexistent.key" ||
        std::strcmp(s_isolated.GetCStr("loc.nonexistent.key"), "loc.nonexistent.key") != 0) {
        s_isolated.UnregisterChangeCallback(obsId);
        s_isolated.Shutdown();
        return fail("FAIL:FALLBACK");
    }
    if (s_isolated.SetLanguage("zz_invalid")) {
        s_isolated.UnregisterChangeCallback(obsId);
        s_isolated.Shutdown();
        return fail("FAIL:INVALID_REJECT");
    }

    s_isolated.UnregisterChangeCallback(obsId);
    s_isolated.Shutdown();

    if (observerCalls != 2u) {
        return fail("FAIL:OBS_COUNT");
    }
    if (outFailureReason) {
        *outFailureReason = "I18N ES/EN/FALLBACK/OBS OK";
    }
    return true;
}

} // namespace Neleo
