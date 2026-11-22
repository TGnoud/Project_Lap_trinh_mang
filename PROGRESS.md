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
