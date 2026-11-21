# RTLAirLink

Driver Wi‑Fi thử nghiệm cho Realtek RTL8852BE trên macOS Tahoe Intel, có AWDL và AirDrop native.

**Triển khai AWDL native do Anh Tài (xnpty) nghiên cứu, thiết kế và phát triển trong Owly/RTLAirLink.** [Ghi công và lịch sử mã nguồn](CREDITS.md)

[![Ủng hộ qua Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/taingo) · [Discord](https://discord.com/users/856029147750858793)

## Cài v0.2.0

Chạy trên macOS Tahoe Intel với OpenCore đã hoạt động và Python 3.9 trở lên.

~~~bash
curl -fL --proto '=https' --proto-redir '=https' -o install-rtlairlink.sh https://github.com/xnpty/RTLAirLink/releases/download/v0.2.0/install-rtlairlink.sh && bash install-rtlairlink.sh
~~~

Trình cài tải gói và các phụ thuộc, kiểm tra SHA‑256, hiện thay đổi OpenCore rồi sao lưu các tệp EFI cần thay. Gói ZIP cũng có Install-RTLAirLink.command để mở trực tiếp. Sau khi cài thành công, khởi động lại để nạp kext. [Hướng dẫn cài](docs/INSTALL.md)

## Kết quả kiểm thử

Mã nguồn làm nền cho bản phát hành đã truyền file hai chiều qua IPv6 trên awdl0 với RTL8852BE, macOS build 25G241 và cấu hình Bluetooth riêng. Mac gửi thành công 6/8 lượt. Chiều ngược lại nhận tám ảnh 922.428 byte, khớp SHA‑256 với ảnh gốc. Hai lượt gửi dừng khi dò dịch vụ Companion. Bộ kext v0.2.0 đã qua kiểm thử build và trình cài. Việc xác nhận sau boot với các bundle ID mới còn chờ. [Chi tiết](docs/STATUS.md)

[Build từ mã nguồn](docs/BUILD.md) · [Giấy phép và ghi công](CREDITS.md)
