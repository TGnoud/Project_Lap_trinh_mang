# AGENTS.md

## Phạm vi

- Đọc `PLAN.md` và ma trận R01–R15 trước khi sửa ứng dụng. Làm theo milestone được yêu cầu.
- `1-Chap1/` đến `4-Chap4/` là code mẫu. `0-Docs/` và `1-Docs/` là tài liệu tham chiếu. Giữ nguyên các bài mẫu, tài liệu và thay đổi có sẵn của người dùng.
- Viết mã ứng dụng trong `src/`, test trong `tests/` và migration trong `migrations/`. Dùng Linux hoặc WSL2 Ubuntu, C++17 và CMake/CTest.

## Quy tắc triển khai

- Dùng TCP POSIX trực tiếp và `poll()` non-blocking. Giữ framing 4 byte big-endian và JSON UTF-8 tối đa 64 KiB. Tách transport, framing, dispatcher, service và repository.
- Vòng `poll()` sở hữu socket. Đưa SQLite và băm mật khẩu sang worker hữu hạn. Kiểm lỗi syscall, giữ offset khi gửi dở và giới hạn bộ đệm mỗi kết nối.
- Lấy danh tính từ phiên server. Kiểm owner và friendship trước thay đổi. Ghi mutation và change-log hoặc inbox trong cùng transaction khi có liên quan.
- Giữ push tự động sau login, ACK idempotent và nguồn `SHARED/shared_by`. Client chỉ thay đổi dữ liệu khi online. Giữ password, token và payload xác thực ngoài log.
- Ưu tiên CLI và các yêu cầu chấm điểm. Giới hạn bản TCP plaintext ở localhost hoặc lab tin cậy.

## Kiểm chứng

- Viết unit test cho module production thay đổi. Dùng TCP thật và DB tạm để kiểm giao thức, quyền, restart và push. Gate line coverage toàn ứng dụng >=91%, mục tiêu branch >=85%.
- Báo lệnh, kết quả thực tế và giới hạn đã xác minh. Ghi rõ test, coverage hoặc benchmark chưa chạy. Giữ binary, DB thử nghiệm và dữ liệu đo thô ngoài Git.
