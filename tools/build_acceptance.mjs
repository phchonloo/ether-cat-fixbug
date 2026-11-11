import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {Workbook,SpreadsheetFile} from '@oai/artifact-tool';
const root=path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const source=await Workbook.fromCSV((await fs.readFile(path.join(root,'实机验收表.csv'),'utf8')).replace(/^\uFEFF/,''),{sheetName:'Source'});
const rows=source.worksheets.getItemAt(0).getRange('A1:I21').values;
if(rows.length!==21||rows[20][0]!=='P20')throw Error('CSV record count');
const wb=Workbook.create(),s=wb.worksheets.add('实机验收');
s.showGridLines=false;s.tabColor='#1F3B5B';
s.getRange('A1:I25').format.font={name:'Arial',size:11,color:'#202C3B'};
s.getRange('A2').values=[['FCM 24V 驱动器实机验收']];s.getRange('A2').format.font={size:16,bold:true};
s.getRange('A3').values=[['来源：15版软件实现及验收计划。填写结果、测试人、日期和证据后再给出结论；目前全部待测。']];
s.getRange('A3').format.font={size:10,italic:true,color:'#576574'};
s.getRange('A5:I25').values=rows;
s.getRange('A5:I5').format={fill:'#1F3B5B',font:{color:'#FFFFFF',bold:true},rowHeightPx:34,horizontalAlignment:'center',verticalAlignment:'center'};
s.getRange('A6:I25').format={wrapText:true,verticalAlignment:'top',rowHeightPx:88};
s.getRange('E6:I25').format.fill='#FFF7DB';
s.getRange('E6:I25').format.font={color:'#174A85'};
const widths=[65,165,380,470,300,100,100,125,280];
for(let i=0;i<9;i++)s.getRangeByIndexes(0,i,25,1).format.columnWidthPx=widths[i];
s.getRange('H6:H25').setNumberFormat('yyyy-mm-dd');
s.getRange('F6:F25').dataValidation={rule:{type:'list',values:['待测','通过','不通过','不适用']}};
s.getRange('F6:F25').conditionalFormats.add('containsText',{text:'不通过',format:{fill:'#FCE8E8',font:{color:'#A32020',bold:true}}});
s.freezePanes.freezeRows(5);s.freezePanes.freezeColumns(2);
wb.recalculate();
for(let i=0;i<20;i++){
 const r=s.getRangeByIndexes(i+5,0,1,9).values[0];
 if(r[0]!==rows[i+1][0]||r[5]!=='待测'||r[4]||r[6]||r[7]||r[8])throw Error('Incorrect untested status '+i);
}
const inspected=await wb.inspect({kind:'table',range:'实机验收!A5:I8',include:'values,formulas',tableMaxRows:4,tableMaxCols:9,maxChars:3500});
const errors=await wb.inspect({kind:'match',searchTerm:'#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A|#NUM!|#NULL!|#SPILL!|#CALC!',options:{useRegex:true,maxResults:20},maxChars:1000});
const out=path.join(root,'outputs/protection_20261102');await fs.mkdir(out,{recursive:true});
await fs.writeFile(path.join(root,'validation/acceptance_inspect.log'),inspected.ndjson+'\n'+errors.ndjson);
for(const [name,range] of [['acceptance_top','A1:I9'],['acceptance_bottom','A18:I25']]){
 const png=await wb.render({sheetName:s.name,range,scale:1,format:'png'});
 await fs.writeFile(path.join(root,'validation',name+'.png'),new Uint8Array(await png.arrayBuffer()));
}
await (await SpreadsheetFile.exportXlsx(wb)).save(path.join(out,'实机验收表.xlsx'));
console.log('PASS 20 unique records; all untested; result/owner/date/evidence blank; inspected and rendered');
