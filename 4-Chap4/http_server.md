# Giao thức và cách xử lý của HTTP server

Chương trình trong `http_server.c` là một máy chủ HTTP viết bằng C, lắng nghe tại `0.0.0.0:8888`. Nó tạo một luồng cho mỗi kết nối TCP. Luồng xử lý dùng `GET` để liệt kê thư mục hoặc gửi tệp, và `POST` để nhận tệp từ biểu mẫu. 

## 1. Giao thức và định dạng dữ liệu

| Tầng hoặc vai trò | Giao thức hay định dạng | Cách chương trình sử dụng |
| --- | --- | --- |
| Tầng mạng | IPv4 | `AF_INET` chọn IPv4. Địa chỉ `0.0.0.0` cho phép lắng nghe trên mọi giao diện IPv4 của máy tại cổng `8888`. |
| Tầng vận chuyển | TCP | `SOCK_STREAM` và `IPPROTO_TCP` tạo socket luồng. Chương trình nhận và gửi dữ liệu qua `recv()` và `send()`. |
| Tầng ứng dụng | HTTP | Chương trình nhận yêu cầu `GET` và `POST`, gửi dòng trạng thái mang phiên bản `HTTP/1.1` rồi đóng kết nối sau khi xử lý một yêu cầu. Dữ liệu HTTP được truyền trực tiếp qua TCP, không có lớp TLS. |
| Nội dung HTTP | HTML và `multipart/form-data` | Trang danh sách thư mục được tạo dưới dạng HTML. Biểu mẫu tải tệp lên dùng `multipart/form-data`, với tham số `boundary` để phân cách các phần trong thân yêu cầu. |



## 2. Tiếp nhận và phân tích yêu cầu

1. `main()` tạo socket TCP, gọi `bind()` để gắn địa chỉ và cổng, rồi gọi `listen(s, 10)` để thiết lập hàng đợi kết nối chờ. Số `10` là tham số của hàng đợi, còn luồng được tạo sau mỗi lần `accept()` thành công.
2. `accept()` trả về socket của kết nối mới. `main()` truyền socket này cho `ClientThread()` qua `pthread_create()` rồi tiếp tục chờ các kết nối khác.
3. `ClientThread()` đọc vào bộ đệm 1 MiB cho đến khi gặp `\r\n\r\n`, tức dòng trống kết thúc phần đầu HTTP, hoặc đến khi `recv()` trả về giá trị không dương. Vì TCP truyền một luồng byte, phần đầu có thể cần nhiều lần đọc, và một lần đọc cũng có thể nhận cả phần đầu lẫn một phần thân yêu cầu.
4. Khi tìm thấy dấu kết thúc phần đầu, luồng dùng `sscanf()` để lấy hai trường đầu của dòng yêu cầu vào `method` và `path`. Nó thay `%20` bằng dấu cách, rồi chọn nhánh `GET` hoặc `POST`. Mã nguồn bỏ qua trường phiên bản HTTP trong dòng yêu cầu và chỉ giải mã `%20` trong đường dẫn.
5. Cuối hàm, `close(c)` đóng socket của kết nối. Yêu cầu thiếu dấu kết thúc phần đầu hoặc dùng phương thức ngoài `GET` và `POST` đi đến bước đóng kết nối mà không có phản hồi HTTP.



## 3. Xử lý `GET`

| Đường dẫn | Xử lý | Phản hồi |
| --- | --- | --- |
| `/` | Dùng `scandir()` để liệt kê thư mục gốc của hệ thống, vì `rootFolder` được đặt là `/`. HTML chứa hai ô chọn tệp (`file1`, `file2`). Tên thư mục dẫn đến thư mục tương ứng, còn các mục khác có `href` rỗng. | `200 OK`, `Content-Length` và thân HTML. |
| Đường dẫn không kết thúc bằng `*` và có chứa `favicon.ico` | Mở tệp `favicon.ico` trong thư mục làm việc của tiến trình và gửi theo từng khối tối đa 1.024 byte. | `200 OK` và `Content-Length` nếu mở được tệp, hoặc `500 Internal Server Error` nếu mở thất bại. |
| Đường dẫn khác không kết thúc bằng `*` | Dùng `scandir()` với đường dẫn nhận được. Liên kết đến thư mục con giữ đường dẫn thư mục, còn liên kết đến các mục khác được thêm dấu `*` ở cuối để chọn nhánh đọc tệp. | `200 OK` và thân HTML. Nếu `scandir()` không trả về mục nào hoặc gặp lỗi, thân HTML hiện `FOLDER EMPTY`. |
| Đường dẫn kết thúc bằng `*` | Bỏ dấu `*`, mở tệp ở chế độ `rb`, dùng `fseek()` và `ftell()` để lấy kích thước, sau đó dùng `fread()` và `send()` để gửi từng khối tối đa 1.024 byte. | `200 OK` kèm `Content-Type` và `Content-Length`, hoặc `500 Internal Server Error` nếu mở tệp thất bại. |

Trong nhánh đọc tệp, server chọn `Content-Type` dựa vào đuôi tệp. `.png` dùng `image/png`, `.pdf` dùng `application/pdf`, `.txt` dùng `text/plain` và các đuôi còn lại dùng `application/octet-stream`. Các phản hồi HTML và favicon chỉ khai báo `Content-Length` trong phần đầu.



## 4. Xử lý `POST` tải tệp lên

1. Biểu mẫu ở trang `/` gửi yêu cầu `POST` đến `/mnt/g/WSL` với `enctype="multipart/form-data"`. Nhánh `POST` không dùng đường dẫn này để chọn thư mục lưu.
2. Server tìm `Content-Length` để lấy số byte của thân yêu cầu và tìm `boundary` trong `Content-Type`. Nó cấp phát bộ đệm theo độ dài đó rồi gọi `recv()` để đọc thân yêu cầu.
3. Server tìm thuộc tính `filename` trong từng phần, lấy dữ liệu sau dòng trống phân cách phần đầu của phần đó với nội dung tệp, rồi dùng `boundary` để xác định điểm kết thúc. Nó mở tệp với `fopen(filename, "wb")` và ghi nội dung bằng `fwrite()`. Tên tệp tương đối được phân giải từ thư mục làm việc của tiến trình.
4. Sau khi vòng lặp xử lý các phần kết thúc, hàm gửi `HTTP/1.1 200 OK` với thân phản hồi rỗng rồi đóng socket. Mã nguồn không kiểm tra kết quả ghi trước khi trả `200 OK`.



## 5. Giới hạn ảnh hưởng đến việc xử lý

- **Đọc thân `POST`.** Nhánh `POST` đọc vào một bộ đệm mới mà không chuyển tiếp các byte thân đã nằm sau `\r\n\r\n` trong bộ đệm ban đầu. Nếu một lần `recv()` nhận cả phần đầu lẫn một phần thân, dữ liệu thân có thể bị thiếu và luồng có thể tiếp tục chờ. Bộ phân tích multipart cũng dựa vào các hàm chuỗi C nên việc tìm ranh giới phần trong dữ liệu nhị phân thiếu tin cậy.
- **Gửi phản hồi.** Chương trình bỏ qua giá trị trả về của `send()`. Nếu một lần gửi chỉ chuyển được một phần số byte yêu cầu, phần còn lại có thể bị thiếu so với `Content-Length`.
- **Xử lý đồng thời.** Các luồng `GET` dùng chung biến toàn cục `currentPath` mà không đồng bộ. Khi nhiều client cùng duyệt thư mục, các lần ghi đồng thời vào biến này có thể làm sai đường dẫn dùng để liệt kê hoặc tạo liên kết.


