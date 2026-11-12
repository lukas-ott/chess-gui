#include "Keychain.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <utility>

// Core Foundation takes and gives bytes as UInt8: the casts to and from char are unavoidable.
// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
namespace kiebitz::app::keychain {
namespace {

/// A Core Foundation object, released when it goes.
template <typename T> class Cf {
public:
    explicit Cf(T object) : m_object(object) {}
    ~Cf() {
        if (m_object != nullptr) {
            CFRelease(m_object);
        }
    }
    Cf(const Cf&) = delete;
    Cf& operator=(const Cf&) = delete;
    Cf(Cf&& other) noexcept : m_object(std::exchange(other.m_object, nullptr)) {}
    Cf& operator=(Cf&&) = delete;
    [[nodiscard]] T get() const { return m_object; }

private:
    T m_object;
};

Cf<CFStringRef> string(const QString& text) {
    const QByteArray utf8 = text.toUtf8();
    return Cf<CFStringRef>(CFStringCreateWithBytes(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(utf8.constData()), utf8.size(),
        kCFStringEncodingUTF8, static_cast<Boolean>(false)));
}

/// The query for the item: a generic password with this service and account.
Cf<CFMutableDictionaryRef> query(const QString& service, const QString& account) {
    Cf<CFMutableDictionaryRef> result(CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
    const Cf<CFStringRef> serviceName = string(service);
    const Cf<CFStringRef> accountName = string(account);
    CFDictionarySetValue(result.get(), kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(result.get(), kSecAttrService, serviceName.get());
    CFDictionarySetValue(result.get(), kSecAttrAccount, accountName.get());
    return result;
}

} // namespace

std::optional<QByteArray> load(const QString& service, const QString& account) {
    const Cf<CFMutableDictionaryRef> search = query(service, account);
    CFDictionarySetValue(search.get(), kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(search.get(), kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef found = nullptr;
    if (SecItemCopyMatching(search.get(), &found) != errSecSuccess || found == nullptr) {
        return std::nullopt;
    }
    const Cf<CFDataRef> data(static_cast<CFDataRef>(found));
    return QByteArray(reinterpret_cast<const char*>(CFDataGetBytePtr(data.get())),
                      static_cast<qsizetype>(CFDataGetLength(data.get())));
}

bool save(const QString& service, const QString& account, const QByteArray& secret) {
    remove(service, account); // replace, not add a second one
    const Cf<CFMutableDictionaryRef> item = query(service, account);
    const Cf<CFDataRef> data(CFDataCreate(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(secret.constData()), secret.size()));
    CFDictionarySetValue(item.get(), kSecValueData, data.get());
    CFDictionarySetValue(item.get(), kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlock);
    return SecItemAdd(item.get(), nullptr) == errSecSuccess;
}

void remove(const QString& service, const QString& account) {
    const Cf<CFMutableDictionaryRef> item = query(service, account);
    SecItemDelete(item.get());
}

} // namespace kiebitz::app::keychain
// NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
