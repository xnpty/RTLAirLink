# RTLAirLink

Driver Wi‑Fi thử nghiệm cho Realtek RTL8852BE trên macOS Tahoe Intel, có AWDL và AirDrop native.

**Triển khai AWDL native do Anh Tài (xnpty) nghiên cứu, thiết kế và phát triển trong Owly/RTLAirLink.** [Ghi công và lịch sử mã nguồn](CREDITS.md)

[![Ủng hộ qua Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/taingo) · [Discord](https://discord.com/users/856029147750858793)

## Yêu cầu

- macOS Tahoe 26 trên Intel, card Realtek RTL8852BE và EFI OpenCore đã khởi động được.
- Python 3.9 trở lên. Trình cài dùng `ocvalidate` của OpenCore 1.0.9 theo mặc định. [Dùng phiên bản OpenCore khác](docs/INSTALL.md)
- Bluetooth hoạt động để AirDrop dò thiết bị.

Wi-Fi cần `Lilu.kext` từ 1.7.3 trở lên và ba kext sau. [Phiên bản và nguồn tải](docs/DEPENDENCIES.md)

- `IOSkywalkFamily.kext`
- `IO80211FamilyLegacy.kext`
- `AMFIPass.kext`

## Cài v0.2.0

~~~bash
curl -fL --proto '=https' --proto-redir '=https' -o install-rtlairlink.sh \
  https://raw.githubusercontent.com/xnpty/RTLAirLink/7541fd08ef2482a774b02e44ed5fa798a04ccd98/tools/install-v0.2.0.sh && \
  bash install-rtlairlink.sh
~~~

Trình cài dùng lại các kext tương thích, tải phần phụ thuộc còn thiếu và bật kext theo đúng thứ tự. Trên Tahoe, trình cài bổ sung `-amfipassbeta` nếu chưa có. Kiểm tra đúng EFI trong bản xem trước, gõ `INSTALL` để sao lưu và áp dụng, rồi khởi động lại. [Thứ tự nạp và hướng dẫn cài](docs/INSTALL.md)

## Khắc phục sự cố và báo lỗi

- **Trình cài báo lỗi cấu hình.** Chạy `bash install-rtlairlink.sh --efi /Volumes/EFI --dry-run` với đường dẫn EFI của bạn. Nếu dùng OpenCore khác 1.0.9, dùng `--ocvalidate` trỏ tới trình kiểm tra đúng phiên bản. [Hướng dẫn cài](docs/INSTALL.md)
- **macOS không khởi động.** Boot từ EFI OpenCore dự phòng và khôi phục các tệp đã thay. [Khôi phục EFI](docs/INSTALL.md#recover-the-efi)
- **Wi-Fi biến mất sau khi khởi động lại.** Kiểm tra máy đã boot đúng EFI và xem các kext Wi-Fi đã nạp chưa.

~~~bash
kmutil showloaded --list-only | grep -Ei 'Lilu|IOSkywalkFamily|IO80211FamilyLegacy|AMFIPass|io\.github\.xnpty\.RTLAirLink'
~~~

- **AirDrop dừng ở Waiting hoặc Sending.** Kiểm tra Bluetooth và IPv6 link-local trên `awdl0` bằng `ifconfig awdl0`. Ghi rõ chiều gửi, hai thiết bị có thấy nhau không và bước bị kẹt.

Khi báo lỗi, gửi phiên bản và build macOS (`sw_vers`), phiên bản OpenCore, mẫu card và PCI ID, các bước tái hiện cùng đầu ra của trình cài hoặc danh sách kext liên quan. Ẩn thông tin nhận dạng thiết bị và mạng riêng trong log trước khi đăng. [Tạo issue trên GitHub](https://github.com/xnpty/RTLAirLink/issues/new)

## Kết quả kiểm thử

Mã nguồn làm nền cho bản phát hành đã truyền file hai chiều qua IPv6 trên awdl0 với RTL8852BE, macOS build 25G241 và cấu hình Bluetooth riêng. Mac gửi thành công 6/8 lượt. Chiều ngược lại nhận tám ảnh 922.428 byte, khớp SHA‑256 với ảnh gốc. Hai lượt gửi dừng khi dò dịch vụ Companion. Bộ kext v0.2.0 đã qua kiểm thử build và trình cài. Việc xác nhận sau boot với các bundle ID mới còn chờ. [Chi tiết](docs/STATUS.md)

[Build từ mã nguồn](docs/BUILD.md) · [Giấy phép và ghi công](CREDITS.md)
