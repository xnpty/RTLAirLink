# RTLAirLink

Driver Wi‑Fi thử nghiệm cho Realtek trên macOS Tahoe, có AWDL và AirDrop native.

**Triển khai AWDL native do Anh Tài (xnpty) nghiên cứu, thiết kế và phát triển trong Owly/RTLAirLink.** [Ghi công và lịch sử mã nguồn](CREDITS.md)

[![Ủng hộ qua Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/taingo) · [Discord](https://discord.com/users/856029147750858793)

## Yêu cầu

- macOS Tahoe 26 trên x86_64 (Intel hoặc AMD), card Realtek RTL8852BE và EFI OpenCore đã khởi động được.
- Python 3.9 trở lên để chạy trình cài.
- AirDrop dùng Bluetooth để dò thiết bị. Trình cài có thể thiết lập Bluetooth Realtek USB tương thích.

Wi-Fi cần `Lilu.kext` từ 1.7.3 trở lên và ba kext sau. [Phiên bản và nguồn tải](docs/DEPENDENCIES.md)

- `IOSkywalkFamily.kext`
- `IO80211FamilyLegacy.kext`
- `AMFIPass.kext`

## Cài v0.2.0

1. Tải và giải nén [RTLAirLink-v0.2.0-Setup.zip](https://github.com/xnpty/RTLAirLink/releases/download/v0.2.0-setup/RTLAirLink-v0.2.0-Setup.zip).
2. Mở `Install-RTLAirLink.command`. Kiểm tra đúng EFI trong bản xem trước, rồi gõ `y` để sao lưu và cài.
3. Khởi động lại macOS.

Trình cài dùng lại kext tương thích, tải các kext Wi-Fi còn thiếu, xếp đúng thứ tự nạp và tự thêm thiết lập OpenCore cần thiết. [Thứ tự nạp và cách cài thủ công](docs/INSTALL.md)

Để cài qua Terminal, chạy lệnh dưới đây.

~~~bash
curl -fLO https://github.com/xnpty/RTLAirLink/releases/download/v0.2.0-setup/install-rtlairlink-setup.sh && \
  bash install-rtlairlink-setup.sh
~~~

Nếu cần cài Bluetooth Realtek USB tương thích, chạy `bash install-rtlairlink-setup.sh --with-bluetooth`. [Thiết lập Bluetooth](docs/INSTALL.md#realtek-bluetooth-for-airdrop)

## Khắc phục sự cố và báo lỗi

- **Trình cài báo lỗi cấu hình.** Chạy `bash install-rtlairlink-setup.sh --efi /Volumes/EFI --dry-run` với đường dẫn EFI của bạn. Nếu bản OpenCore có cấu trúc cấu hình khác, dùng `--ocvalidate` trỏ tới trình kiểm tra đi kèm bản đó. [Hướng dẫn kiểm tra EFI](docs/INSTALL.md#preview-and-validate)
- **macOS không khởi động.** Boot từ EFI OpenCore dự phòng và khôi phục các tệp đã thay. [Khôi phục EFI](docs/INSTALL.md#recover-the-efi)
- **Wi-Fi biến mất sau khi khởi động lại.** Kiểm tra máy đã boot đúng EFI và xem các kext Wi-Fi đã nạp chưa.

~~~bash
kmutil showloaded --list-only | grep -Ei 'Lilu|IOSkywalkFamily|IO80211FamilyLegacy|AMFIPass|io\.github\.xnpty\.RTLAirLink|BlueToolFixup|RealtekBluetoothFirmware'
~~~

- **AirDrop dừng ở Waiting hoặc Sending.** Kiểm tra Bluetooth và IPv6 link-local trên `awdl0` bằng `ifconfig awdl0`. Ghi rõ chiều gửi, hai thiết bị có thấy nhau không và bước bị kẹt.

Khi báo lỗi, gửi phiên bản và build macOS (`sw_vers`), phiên bản OpenCore, mẫu card và PCI ID, các bước tái hiện cùng đầu ra của trình cài hoặc danh sách kext liên quan. Ẩn thông tin nhận dạng thiết bị và mạng riêng trong log trước khi đăng. [Tạo issue trên GitHub](https://github.com/xnpty/RTLAirLink/issues/new)

## Kết quả kiểm thử

Mã nguồn làm nền cho bản phát hành đã truyền file hai chiều qua IPv6 trên awdl0 với RTL8852BE, macOS build 25G241 và cấu hình Bluetooth riêng. Mac gửi thành công 6/8 lượt. Chiều ngược lại nhận tám ảnh 922.428 byte, khớp SHA‑256 với ảnh gốc. Hai lượt gửi dừng khi dò dịch vụ Companion. Bộ kext v0.2.0 đã qua kiểm thử build và trình cài. Việc xác nhận sau boot với các bundle ID mới còn chờ. [Chi tiết](docs/STATUS.md)

[Build từ mã nguồn](docs/BUILD.md) · [Giấy phép và ghi công](CREDITS.md)
