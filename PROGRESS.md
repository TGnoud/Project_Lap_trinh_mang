# Tiến độ IT4062

Nhật ký trạng thái theo từng task. Dùng mã `D01`–`D28` từ [PLAN.md](PLAN.md) hoặc mã riêng cho việc bổ sung. Thêm dòng mới khi bắt đầu và khi kết thúc để giữ lịch sử.

| Ngày | Task | Trạng thái | Phạm vi, kết quả hoặc trở ngại |
|---|---|---|---|
| 22/11/2026 | DOC-01 | `IN_PROGRESS` | Thêm quy tắc ghi trạng thái task vào `AGENTS.md` và tạo nhật ký tiến độ. |
| 22/11/2026 | DOC-01 | `DONE` | Đã thêm quy tắc trước/sau task. Đã kiểm liên kết giữa các tệp và chạy `git diff --check`. |
| 22/11/2026 | D01/W1 | `IN_PROGRESS` | Tạo nền CMake C++17, đăng ký kiểm thử cấu hình trong CTest và thêm CI Linux với gate coverage. |
| 22/11/2026 | D01/W1 | `DONE` | Build trên Ubuntu WSL2 với GCC 15.2.0 và CMake 3.31.10. `ctest -L w1` đạt 7/7. gcovr 8.5 đo line 100% (62/62), vượt gate 91%, và branch 73,8% (90/122), dưới mục tiêu 85%. Đã kiểm đủ 3 tệp nguồn trong báo cáo `build/d01-check/coverage.json`. Giữ báo cáo test tại `build/d01-check/ctest.xml`. Workflow GitHub Actions chưa chạy trên GitHub. |
| 22/11/2026 | DOC-02 | `IN_PROGRESS` | Viết tài liệu giải thích mã D01/W1 để phản biện và bổ sung quy tắc cho các task sau. |
| 23/11/2026 | DOC-02 | `DONE` | Đã viết [tài liệu phản biện D01/W1](docs/phan-bien/D01-W1.md) và thêm quy tắc vào `AGENTS.md`. Đã kiểm liên kết và đối chiếu số liệu với báo cáo CTest/gcovr của D01. |
| 23/11/2026 | DOC-03 | `IN_PROGRESS` | Thêm `docs/` vào `.gitignore` và rà các tệp build, dữ liệu chạy cục bộ và đầu ra bài lab. |
| 23/11/2026 | DOC-03 | `DONE` | Đã bỏ qua `docs/`, đầu ra build/test, dữ liệu chạy cục bộ và binary bài lab. `git check-ignore` xác nhận 18 đường dẫn bỏ qua và 13 đường dẫn giữ lại. `docs/` chưa có tệp được Git theo dõi. [Ghi chú kiểm tra](docs/phan-bien/DOC-03.md) lưu cục bộ. |
| 23/11/2026 | DOC-04 | `IN_PROGRESS` | Gỡ workflow CI và rà lại kế hoạch theo ba yêu cầu về chức năng, unit test/coverage và bài đo tải. |
| 23/11/2026 | DOC-04 | `DONE` | Đã gỡ workflow CI khỏi repo và vùng staged. Kế hoạch giữ đủ 15 tiêu chí, kiểm line coverage >90% trên WSL và một bài tải TCP ở 20/50/100/200 client. CTest D01 đạt 7/7. Script chấp nhận báo cáo D01 100%, từ chối 90% và chấp nhận 90,5%. [Ghi chú phản biện](docs/phan-bien/DOC-04.md). |
| 23/11/2026 | DOC-05 | `IN_PROGRESS` | Viết lại 28 thông điệp commit gợi ý trong `PLAN.md` bằng tiếng Việt. |
| 23/11/2026 | DOC-05 | `DONE` | Đã dịch 28 thông điệp, giữ tiền tố và nội dung các task. Đã kiểm đủ 7 commit dự kiến mỗi tuần và chạy `git diff --check`. [Ghi chú](docs/phan-bien/DOC-05.md). |
| 23/11/2026 | D02/W1 | `IN_PROGRESS` | Làm vòng đời TCP POSIX theo mẫu C, giữ C++17 và gọi `close()` trực tiếp. Kiểm socket thật trong nhóm U-02. |
| 23/11/2026 | D02/W1 | `DONE` | Build và CTest trên Ubuntu WSL2 đạt 8/8. U-02 kiểm socket loopback và xác minh không rò fd khi bind/connect lỗi. Line coverage 91,49% (129/141). Kết quả ở `build/d02-check/`. [Tài liệu phản biện D02](docs/phan-bien/D02-W1.md). |
| 23/11/2026 | D01/W1 | `IN_PROGRESS` | Chuyển CMake, cấu hình dòng lệnh và unit test sang C theo phương án đã chốt. |
| 23/11/2026 | D02/W1 | `IN_PROGRESS` | Chuyển hai chương trình TCP, module socket và U-02 sang C, bám trình tự của bài mẫu. |
| 23/11/2026 | D01/W1 | `DONE` | CMake chỉ bật C, build mới với GCC 15.2.0 ở chế độ `gnu11` và chạy unit test C qua CTest. `config.c` đạt 47/47 dòng. [Tài liệu phản biện D01 bản C](docs/phan-bien/D01-W1.md). |
| 23/11/2026 | D02/W1 | `DONE` | Đã chuyển server/client TCP và U-02 sang C, giữ code mẫu và test Python. Build `build/c-w1-check/` chạy 8/8 mục CTest, gồm kết nối TCP thật. Coverage của D01/D02 bản C đạt 90,48% (114/126 dòng), qua ngưỡng >90%. [Tài liệu phản biện D02 bản C](docs/phan-bien/D02-W1.md). |
| 23/11/2026 | D03/W1 | `IN_PROGRESS` | Viết codec frame 4 byte + JSON UTF-8 và demo echo theo bài mẫu. Thêm U-01, TCP-01, CT-01 và chạy lại coverage trên WSL. |
| 23/11/2026 | D03/W1 | `DONE` | Đã triển khai codec frame và echo JSON trên một kết nối. Ubuntu WSL2 chạy `ctest -L w1` đạt 11/11, gồm U-01, TCP-01, CT-01. Line coverage 92,57% (299/323 dòng, đủ 6 tệp nguồn), branch 91,7% (244/266). Báo cáo tại `build/d03-final/ctest.xml` và `build/d03-final/coverage.json`. Test chưa chủ động gây `EINTR` hoặc short write. `poll()` thuộc D04, bài đo tải thuộc W4. [Tài liệu phản biện D03](docs/phan-bien/D03-W1.md). |
| 25/11/2026 | D04/W1 | `IN_PROGRESS` | Chuyển server echo sang POSIX non-blocking và `poll()` theo bài chat room. Thêm RX/TX, deadline, U-02 mở rộng, TCP-02 và đo lại coverage trên WSL. |
| 25/11/2026 | D04/W1 | `DONE` | Server C xử lý nhiều kết nối bằng `poll()`, giữ RX/TX theo fd và thu hồi socket quá hạn. CLI giới hạn kết nối TCP 5 giây. Ubuntu WSL2 chạy `ctest -L w1` đạt 13/13, gồm U-02 mở rộng và TCP-02 qua TCP thật với client chậm, half-close, timeout, thu hồi fd và trần 256 kết nối. Line coverage 91,91% (511/556 dòng, đủ 8 tệp nguồn), branch 87,6% (396/452). Báo cáo tại `build/d04-verify/ctest.xml` và `build/d04-verify/coverage.json`. Worker thuộc D05 và benchmark thuộc W4. [Tài liệu phản biện D04](docs/phan-bien/D04-W1.md). |
