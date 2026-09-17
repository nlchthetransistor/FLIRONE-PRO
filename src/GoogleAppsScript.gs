const SHEET_ID = "18MHvEOmOhL7MUYIl6wWVylUwLFhgYYnxgKPXBiU1KRQ"; // ← THAY SHEET ID CỦA BẠN

function doPost(e) {
  try {
    const ss = SpreadsheetApp.openById(SHEET_ID);
    const data = JSON.parse(e.postData.contents);
    
    // Lấy ngày hiện tại (YYYY-MM-DD)
    const today = new Date();
    const dateStr = Utilities.formatDate(today, Session.getScriptTimeZone(), 'yyyy-MM-dd');
    
    // Kiểm tra sheet cho ngày hôm nay, nếu không có thì tạo
    let sheet = ss.getSheetByName(dateStr);
    if (!sheet) {
      // Sao chép template để tạo sheet mới
      const template = ss.getSheetByName("Template");
      if (!template) {
        return ContentService.createTextOutput(JSON.stringify({
          success: false,
          error: "Template sheet not found"
        })).setMimeType(ContentService.MIME_TYPE_JSON);
      }
      
      sheet = template.copyTo(ss);
      sheet.setName(dateStr);
      
      // Xóa dữ liệu cũ (giữ header)
      const range = sheet.getRange(2, 1, sheet.getMaxRows() - 1, sheet.getMaxColumns());
      range.clearContent();
    }
    
    // Thêm dữ liệu vào hàng tiếp theo
    const lastRow = sheet.getLastRow();
    const newRow = lastRow + 1;
    
    sheet.getRange(newRow, 1).setValue(data.timestamp);           // Cột 1: Timestamp
    sheet.getRange(newRow, 2).setValue(data.patient_name);        // Cột 2: Patient Name
    sheet.getRange(newRow, 3).setValue(data.patient_age);         // Cột 3: Patient Age (NEW)
    sheet.getRange(newRow, 4).setValue(data.patient_gender);      // Cột 4: Patient Gender (NEW)
    sheet.getRange(newRow, 5).setValue(data.measurement_type);    // Cột 5: Measurement Type
    sheet.getRange(newRow, 6).setValue(data.temperature);         // Cột 6: Temperature
    sheet.getRange(newRow, 7).setValue(data.emissivity);          // Cột 7: Emissivity
    sheet.getRange(newRow, 8).setValue(data.refl_offset);         // Cột 8: Refl Offset
    sheet.getRange(newRow, 9).setValue(data.raw_scale);           // Cột 9: Raw Scale
    sheet.getRange(newRow, 10).setValue(data.temp_offset);        // Cột 10: Temp Offset
    sheet.getRange(newRow, 11).setValue(data.ambient_temp);       // Cột 11: Ambient Temp
    sheet.getRange(newRow, 12).setValue(data.humidity);           // Cột 12: Humidity
    
    return ContentService.createTextOutput(JSON.stringify({
      success: true,
      message: "Data saved successfully",
      sheet: dateStr,
      row: newRow
    })).setMimeType(ContentService.MIME_TYPE_JSON);
    
  } catch (error) {
    return ContentService.createTextOutput(JSON.stringify({
      success: false,
      error: error.toString()
    })).setMimeType(ContentService.MIME_TYPE_JSON);
  }
}
