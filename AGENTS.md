# AGENTS.md

## Theo dõi trạng thái task

- Với mỗi task, trước khi bắt đầu triển khai, thêm một dòng `IN_PROGRESS` vào [PROGRESS.md](PROGRESS.md) với mã task và phạm vi dự kiến. Sau khi triển khai và kiểm tra, thêm dòng mới cùng mã với trạng thái `DONE`, hoặc `BLOCKED` nếu còn trở ngại. Ghi kết quả thực tế, kiểm tra đã chạy hoặc lý do chưa chạy, và việc còn lại nếu có. Giữ cả dòng trước và sau để theo dõi lịch sử.

## Tài liệu phản biện

- Trước khi đánh dấu `DONE`, tạo hoặc cập nhật `docs/phan-bien/<mã-task>.md`, dùng dấu `-` thay cho `/` trong tên tệp, như [D01-W1.md](docs/phan-bien/D01-W1.md). Dẫn link trong dòng kết quả của `PROGRESS.md`. Viết bằng tiếng Việt để người làm đồ án có thể tự trình bày mục đích, tệp cần đọc, luồng mã, lý do các quyết định chính, cách demo và kết quả kiểm tra thực tế. Thêm câu hỏi phản biện sát với mã và ghi rõ giới hạn của phần đã làm. Với task chỉ sửa tài liệu, giải thích phần thay đổi và cách kiểm tương ứng.

## Phạm vi

- Đọc `PLAN.md` và ma trận R01–R15 trước khi sửa ứng dụng. Làm theo milestone được yêu cầu.
- `1-Chap1/` đến `4-Chap4/` là code mẫu. `0-Docs/` và `1-Docs/` là tài liệu tham chiếu. Giữ nguyên các bài mẫu, tài liệu và thay đổi có sẵn của người dùng.
- Viết mã ứng dụng bằng C trong `src/`, test trong `tests/` và migration trong `migrations/`. Dùng Linux hoặc WSL2 Ubuntu, GCC ở chế độ `gnu11` như bài lab và CMake/CTest. Unit test viết bằng C. Python dùng cho test TCP, báo cáo và đo tải.

## Quy tắc triển khai

- Dùng lại code mẫu môn học khi đáp ứng yêu cầu. Ghi chú nguồn và chỉ bổ sung phần mẫu chưa có hoặc sửa phần mẫu chưa đáp ứng điều kiện. Quản lý socket bằng `int fd` và lời gọi `close()` trực tiếp. Với đề xuất mới vượt phạm vi task hoặc thay đổi cách đã thống nhất, giải thích vì sao đề xuất đó tốt hơn cho đồ án, hỏi người dùng và chờ xác nhận trước khi áp dụng.
- Dùng TCP POSIX trực tiếp và `poll()` non-blocking. Giữ framing 4 byte big-endian và JSON UTF-8 tối đa 64 KiB. Dùng json-c từ task triển khai JSON. Tách transport, framing, dispatcher, service và repository.
- Vòng `poll()` sở hữu socket. Đưa SQLite và băm mật khẩu sang worker hữu hạn. Kiểm lỗi syscall, giữ offset khi gửi dở và giới hạn bộ đệm mỗi kết nối.
- Lấy danh tính từ phiên server. Kiểm owner và friendship trước thay đổi. Ghi mutation và change-log hoặc inbox trong cùng transaction khi có liên quan.
- Giữ push tự động sau login, ACK idempotent và nguồn `SHARED/shared_by`. Client chỉ thay đổi dữ liệu khi online. Giữ password, token và payload xác thực ngoài log.
- Ưu tiên CLI và các yêu cầu chấm điểm. Giới hạn bản TCP plaintext ở localhost hoặc lab tin cậy.

## Kiểm chứng

- Viết unit test cho module production thay đổi. Dùng TCP thật và DB tạm để kiểm giao thức, quyền, restart và push. Chạy CTest và xác minh line coverage toàn ứng dụng **>90%** bằng gcovr và script kiểm báo cáo trên Linux/WSL. Có một bài đo tải TCP tái lập được với nhiều mức kết nối đồng thời.
- Báo lệnh, kết quả thực tế và giới hạn đã xác minh. Ghi rõ test, coverage hoặc benchmark chưa chạy. Giữ binary, DB thử nghiệm và dữ liệu đo thô ngoài Git.
