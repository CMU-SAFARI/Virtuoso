import xlsxwriter

filename = "result.xlsx"

workbook = xlsxwriter.Workbook(filename)
worksheet = workbook.add_worksheet()

expenses = (['Rent', 1000], ['Gas', 100])

row =0
col =0

for item, cst in expenses:
    worksheet.write(row, col, item)
    worksheet.write(row, col+1, cost)
    row+=1

worksheet.write(row, 0, 'Total')
worksheet.write(row, 1, '=SUM(B1:B4)')

workbook.close()
