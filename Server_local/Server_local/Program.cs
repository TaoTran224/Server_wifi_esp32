using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text;
using System.Threading.Tasks;

class Program
{
    private const int PORT = 502; // Cổng Modbus TCP chuẩn
    private static TcpListener? server;
    private static readonly object logLock = new object();

    // Chuỗi Hex bổ sung gửi sau 15 giây: 02 30 31 32 33 34 35 36 37 38 39 3A 3B 3C 03
    private static readonly byte[] DelayedPayload = new byte[]
    {
        0x02, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x03
    };

    static async Task Main(string[] args)
    {
        Console.OutputEncoding = Encoding.UTF8;
        Console.InputEncoding = Encoding.UTF8;

        Console.Title = "PC Modbus TCP Server - Precise RTC & Client Port Logging";
        Console.WriteLine("==========================================================");
        Console.WriteLine("   MODBUS TCP SERVER C# - CLIENT IP:PORT & RTC LOGGING   ");
        Console.WriteLine("==========================================================");

        string localIP = GetLocalIPAddress();

        try
        {
            server = new TcpListener(IPAddress.Any, PORT);
            server.Start();

            Console.WriteLine($"[+] Server Modbus TCP đã khởi chạy!");
            Console.WriteLine($"[+] Local IP máy tính : {localIP}");
            Console.WriteLine($"[+] Cổng Modbus TCP   : {PORT}\n");

            WriteLog("SERVER", "INFO", $"Server khởi chạy thành công tại IP: {localIP}, Port: {PORT}");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[Lỗi khởi tạo Server]: {ex.Message}");
            Console.WriteLine("Lưu ý: Nếu dùng Port 502, hãy chạy ứng dụng dưới quyền Administrator.");
            return;
        }

        while (true)
        {
            try
            {
                TcpClient client = await server.AcceptTcpClientAsync();
                _ = Task.Run(() => HandleModbusClientAsync(client));
            }
            catch (Exception ex)
            {
                WriteLog("SERVER", "ERROR", $"Lỗi AcceptClient: {ex.Message}");
                break;
            }
        }
    }

    private static string GetLocalIPAddress()
    {
        try
        {
            foreach (var netInterface in NetworkInterface.GetAllNetworkInterfaces())
            {
                if (netInterface.OperationalStatus == OperationalStatus.Up &&
                    netInterface.NetworkInterfaceType != NetworkInterfaceType.Loopback)
                {
                    var ipProps = netInterface.GetIPProperties();
                    foreach (var addr in ipProps.UnicastAddresses)
                    {
                        if (addr.Address.AddressFamily == AddressFamily.InterNetwork)
                        {
                            return addr.Address.ToString();
                        }
                    }
                }
            }
        }
        catch { }

        return "127.0.0.1";
    }

    private static async Task HandleModbusClientAsync(TcpClient client)
    {
        client.NoDelay = true; // Bỏ thuật toán Nagle để đảm bảo thời gian gửi tức thì

        IPEndPoint? remoteEndPoint = client.Client.RemoteEndPoint as IPEndPoint;
        string clientIP = remoteEndPoint?.Address.ToString() ?? "Unknown";
        int clientPort = remoteEndPoint?.Port ?? 0;

        // Hiển thị cả IP lẫn Port của thiết bị Client
        string deviceName = $"ESP32_{clientIP}:{clientPort}";

        Console.WriteLine($"[{GetRtcTimestamp()}] [+] Kết nối mới từ: {deviceName}");
        WriteLog(deviceName, "INFO", "Kết nối Socket thành công.");

        using (NetworkStream stream = client.GetStream())
        {
            byte[] readBuffer = new byte[2048];
            List<byte> streamBuffer = new List<byte>();

            try
            {
                int bytesRead;
                while ((bytesRead = await stream.ReadAsync(readBuffer, 0, readBuffer.Length)) > 0)
                {
                    // Lấy thời điểm RTC chính xác khi nhận gói TCP
                    DateTime recvTime = DateTime.Now;
                    string recvTimeString = recvTime.ToString("yyyy-MM-dd HH:mm:ss.fff");

                    // 1. Tích lũy byte vào bộ đệm
                    for (int i = 0; i < bytesRead; i++)
                    {
                        streamBuffer.Add(readBuffer[i]);
                    }

                    // 2. Lặp giải mã & quét Header
                    while (streamBuffer.Count >= 7)
                    {
                        // Tìm vị trí Protocol ID = 0x0000
                        int validHeaderIndex = -1;

                        for (int i = 0; i <= streamBuffer.Count - 7; i++)
                        {
                            if (streamBuffer[i + 2] == 0x00 && streamBuffer[i + 3] == 0x00)
                            {
                                validHeaderIndex = i;
                                break;
                            }
                        }

                        // TRƯỜNG HỢP 1: Không có Header chuẩn
                        if (validHeaderIndex == -1)
                        {
                            int discardLength = streamBuffer.Count - 3;
                            if (discardLength > 0)
                            {
                                byte[] invalidBytes = streamBuffer.GetRange(0, discardLength).ToArray();
                                streamBuffer.RemoveRange(0, discardLength);

                                LogInvalidData(deviceName, "[FRAME KHÔNG HỢP LỆ / SAI PROTOCOL ID]", invalidBytes, recvTimeString);
                            }
                            break;
                        }

                        // TRƯỜNG HỢP 2: Dữ liệu rác trước Header
                        if (validHeaderIndex > 0)
                        {
                            byte[] junkBytes = streamBuffer.GetRange(0, validHeaderIndex).ToArray();
                            streamBuffer.RemoveRange(0, validHeaderIndex);

                            LogInvalidData(deviceName, "[DỮ LIỆU RÁC TRƯỚC HEADER]", junkBytes, recvTimeString);
                        }

                        // TRƯỜNG HỢP 3: Header chuẩn Modbus TCP
                        ushort transId = (ushort)((streamBuffer[0] << 8) | streamBuffer[1]);
                        ushort protoId = (ushort)((streamBuffer[2] << 8) | streamBuffer[3]);
                        ushort length = (ushort)((streamBuffer[4] << 8) | streamBuffer[5]);
                        byte unitId = streamBuffer[6];

                        if (length < 2 || length > 253)
                        {
                            byte[] badHeader = streamBuffer.GetRange(0, 7).ToArray();
                            streamBuffer.RemoveAt(0);
                            LogInvalidData(deviceName, $"[KÍCH THƯỚC LENGTH SAI] Length = {length}", badHeader, recvTimeString);
                            continue;
                        }

                        int totalFrameSize = 6 + length;

                        if (streamBuffer.Count < totalFrameSize)
                        {
                            break; // Chưa đủ cả Frame, chờ thêm
                        }

                        // Tách Frame Modbus
                        byte[] requestFrame = streamBuffer.GetRange(0, totalFrameSize).ToArray();
                        byte functionCode = requestFrame[7];

                        if (!IsValidFunctionCode(functionCode, requestFrame.Length))
                        {
                            LogInvalidData(deviceName, $"[FUNCTION CODE LỖI 0x{functionCode:X2}]", requestFrame, recvTimeString);

                            streamBuffer.RemoveRange(0, totalFrameSize);
                            byte[] errResponse = BuildExceptionResponse(transId, unitId, functionCode, 0x01);

                            await stream.WriteAsync(errResponse, 0, errResponse.Length);
                            await stream.FlushAsync();

                            DateTime errSendTime = DateTime.Now;
                            string errSendTimeString = errSendTime.ToString("yyyy-MM-dd HH:mm:ss.fff");
                            WriteLog(deviceName, "SEND_ERR", $"[RECV: {recvTimeString}] [SEND: {errSendTimeString}] Responsive Exception Sent");
                            continue;
                        }

                        // --- BẢN TIN MODBUS HỢP LỆ ---
                        streamBuffer.RemoveRange(0, totalFrameSize);

                        string frameHex = BitConverter.ToString(requestFrame).Replace("-", " ");

                        // Tạo bản tin phản hồi tức thì
                        byte[] responseFrame = BuildModbusResponse(transId, unitId, functionCode, requestFrame);

                        DateTime sendTime = DateTime.Now;
                        string sendTimeString = sendTime.ToString("yyyy-MM-dd HH:mm:ss.fff");

                        // 1. GỬI PHẢN HỒI TỨC THÌ
                        if (responseFrame.Length > 0)
                        {
                            await stream.WriteAsync(responseFrame, 0, responseFrame.Length);
                            await stream.FlushAsync();

                            sendTime = DateTime.Now; // Cập nhật thời điểm gửi xong
                            sendTimeString = sendTime.ToString("yyyy-MM-dd HH:mm:ss.fff");
                        }

                        double deltaMs = (sendTime - recvTime).TotalMilliseconds;
                        string respHex = BitConverter.ToString(responseFrame).Replace("-", " ");

                        // In Terminal gói phản hồi tức thì
                        Console.ForegroundColor = ConsoleColor.Green;
                        Console.WriteLine($"\n[NHẬN HỢP LỆ] [{deviceName} -> PC]");
                        Console.WriteLine($"  - Thời điểm nhận (RTC) : {recvTimeString}");
                        Console.WriteLine($"  - HEX Nhận             : {frameHex}");
                        Console.ResetColor();

                        Console.ForegroundColor = ConsoleColor.Cyan;
                        Console.WriteLine($"[GỬI PHẢN HỒI TỨC THÌ] [PC -> {deviceName}]");
                        Console.WriteLine($"  - Thời điểm gửi (RTC)  : {sendTimeString} (Độ trễ: {deltaMs:F2} ms)");
                        Console.WriteLine($"  - HEX Gửi              : {respHex}");
                        Console.ResetColor();

                        // Ghi log gói tức thì
                        WriteLog(deviceName, "RECV_OK", $"[RTC: {recvTimeString}] {frameHex}");
                        WriteLog(deviceName, "SEND_OK", $"[RTC: {sendTimeString}] [Latency: {deltaMs:F2}ms] {respHex}");

                        // 2. KÍCH HOẠT TIẾN TRÌNH CHỜ 15 GIÂY VÀ GỬI BẢN TIN BỔ SUNG (NON-BLOCKING)
                        _ = Task.Run(async () =>
                        {
                            try
                            {
                                await Task.Delay(15000);

                                if (client.Connected)
                                {
                                    await stream.WriteAsync(DelayedPayload, 0, DelayedPayload.Length);
                                    await stream.FlushAsync();

                                    string delayedSendTimeString = GetRtcTimestamp();
                                    string delayedHex = BitConverter.ToString(DelayedPayload).Replace("-", " ");

                                    Console.ForegroundColor = ConsoleColor.Yellow;
                                    Console.WriteLine($"\n[GỬI BỔ SUNG SAU 15s] [PC -> {deviceName}]");
                                    Console.WriteLine($"  - Thời điểm gửi (RTC)  : {delayedSendTimeString}");
                                    Console.WriteLine($"  - HEX Gửi              : {delayedHex}");
                                    Console.ResetColor();

                                    WriteLog(deviceName, "SEND_DELAYED_15S", $"[RTC: {delayedSendTimeString}] {delayedHex}");
                                }
                            }
                            catch (Exception ex)
                            {
                                WriteLog(deviceName, "WARN", $"Lỗi gửi bản tin bổ sung 15s: {ex.Message}");
                            }
                        });
                    }
                }
            }
            catch (Exception ex)
            {
                WriteLog(deviceName, "WARN", $"Lỗi giao tiếp: {ex.Message}");
            }
            finally
            {
                Console.WriteLine($"\n[{GetRtcTimestamp()}] [-] Thiết bị [{deviceName}] đã ngắt kết nối.");
                WriteLog(deviceName, "INFO", "Đã ngắt kết nối.");
            }
        }

        client.Close();
    }

    private static bool IsValidFunctionCode(byte functionCode, int totalFrameLength)
    {
        switch (functionCode)
        {
            case 0x01:
            case 0x02:
            case 0x03:
            case 0x04:
            case 0x05:
            case 0x06:
                return totalFrameLength == 12;

            case 0x0F:
            case 0x10:
                return totalFrameLength >= 13;

            default:
                return false;
        }
    }

    private static byte[] BuildModbusResponse(ushort transId, byte unitId, byte functionCode, byte[] request)
    {
        List<byte> pdu = new List<byte>();

        switch (functionCode)
        {
            case 0x03:
            case 0x04:
                {
                    ushort regCount = (ushort)((request[10] << 8) | request[11]);
                    byte byteCount = (byte)(regCount * 2);

                    pdu.Add(functionCode);
                    pdu.Add(byteCount);

                    for (int i = 0; i < regCount; i++)
                    {
                        pdu.Add(0x00);
                        pdu.Add((byte)(0x0A + i));
                    }
                    break;
                }

            case 0x06:
                {
                    pdu.Add(functionCode);
                    pdu.Add(request[8]);
                    pdu.Add(request[9]);
                    pdu.Add(request[10]);
                    pdu.Add(request[11]);
                    break;
                }

            case 0x10:
                {
                    pdu.Add(functionCode);
                    pdu.Add(request[8]);
                    pdu.Add(request[9]);
                    pdu.Add(request[10]);
                    pdu.Add(request[11]);
                    break;
                }
        }

        ushort responseLength = (ushort)(1 + pdu.Count);
        byte[] response = new byte[7 + pdu.Count];

        response[0] = (byte)(transId >> 8);
        response[1] = (byte)(transId & 0xFF);
        response[2] = 0x00;
        response[3] = 0x00;
        response[4] = (byte)(responseLength >> 8);
        response[5] = (byte)(responseLength & 0xFF);
        response[6] = unitId;

        pdu.CopyTo(response, 7);

        return response;
    }

    private static byte[] BuildExceptionResponse(ushort transId, byte unitId, byte functionCode, byte exceptionCode)
    {
        byte[] response = new byte[9];
        response[0] = (byte)(transId >> 8);
        response[1] = (byte)(transId & 0xFF);
        response[2] = 0x00;
        response[3] = 0x00;
        response[4] = 0x00;
        response[5] = 0x03;
        response[6] = unitId;
        response[7] = (byte)(functionCode | 0x80);
        response[8] = exceptionCode;

        return response;
    }

    private static void LogInvalidData(string deviceName, string reason, byte[] rawData, string rtcTime)
    {
        string hexData = BitConverter.ToString(rawData).Replace("-", " ");

        Console.ForegroundColor = ConsoleColor.Red;
        Console.WriteLine($"\n[CẢNH BÁO BẢN TIN LỖI/RÁC] [{deviceName}]");
        Console.WriteLine($"  - Thời điểm (RTC) : {rtcTime}");
        Console.WriteLine($"  - Lý do           : {reason}");
        Console.WriteLine($"  - HEX             : {hexData}");
        Console.ResetColor();

        WriteLog(deviceName, "INVALID_FRAME", $"[RTC: {rtcTime}] {reason} | RAW_HEX: {hexData}");
    }

    private static string GetRtcTimestamp()
    {
        return DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff");
    }

    private static void WriteLog(string deviceName, string level, string logContent)
    {
        try
        {
            string logFolder = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Logs");
            if (!Directory.Exists(logFolder))
            {
                Directory.CreateDirectory(logFolder);
            }

            string fileName = $"modbus_log_{DateTime.Now:yyyy-MM-dd}.txt";
            string filePath = Path.Combine(logFolder, fileName);
            string formattedLog = $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}] [{level}] [{deviceName}] {logContent}";

            lock (logLock)
            {
                File.AppendAllText(filePath, formattedLog + Environment.NewLine, Encoding.UTF8);
            }
        }
        catch { }
    }
}