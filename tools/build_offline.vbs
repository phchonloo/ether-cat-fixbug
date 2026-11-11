Option Explicit
Dim fso, base, outdir, source, dte, solution, project, system, plc, sb, i, last, errors, item, panes, pane, ep, imported, outputText
Set fso=CreateObject("Scripting.FileSystemObject")
base=fso.GetParentFolderName(fso.GetParentFolderName(WScript.ScriptFullName))
outdir=fso.BuildPath(base,"_build_home_x1_20261102")
source=fso.BuildPath(base,"PLC\FCM_Homing.plcproj")
If fso.FolderExists(outdir) Then WScript.Echo "Output exists": WScript.Quit 2
fso.CreateFolder outdir
Set dte=Nothing
Sub Fail(where)
    Dim msg
    msg=Err.Description
    WScript.Echo "FAIL " & where & ": " & msg
    On Error Resume Next
    If Not dte Is Nothing Then dte.Quit
    WScript.Quit 2
End Sub
On Error Resume Next
WScript.Echo "OFFLINE ONLY: isolated hidden XAE; no activate, login, download, scan or motion."
Set dte=CreateObject("TcXaeShell.DTE.15.0")
If Err.Number<>0 Then Fail "Create XAE"
dte.SuppressUI=True
dte.MainWindow.Visible=False
Err.Clear
Set solution=Nothing
For i=1 To 120
    Set solution=dte.Solution
    If Err.Number=0 Then
        If Not solution Is Nothing Then Exit For
    End If
    Err.Clear
    WScript.Sleep 250
Next
If solution Is Nothing Then Fail "XAE Solution unavailable"
solution.Create outdir,"FCM_Homing_Offline"
If Err.Number<>0 Then Fail "Create solution"
Set project=solution.AddFromTemplate("C:\TwinCAT\3.1\Components\Base\PrjTemplate\TwinCAT Project.tsproj",fso.BuildPath(outdir,"System"),"FCM_Homing_Offline",False)
If Err.Number<>0 Then Fail "Add system template"
Set system=Nothing
For i=1 To 120
    Set system=project.Object
    If Err.Number=0 Then
        If Not system Is Nothing Then Exit For
    End If
    Err.Clear
    WScript.Sleep 250
Next
If system Is Nothing Then Fail "System unavailable"
Set plc=system.LookupTreeItem("TIPC")
If Err.Number<>0 Then Fail "Lookup TIPC"
WScript.Echo "Import source: " & source & "; exists=" & fso.FileExists(source)
Set imported=plc.CreateChild("FCM_Homing",0,"",CStr(source))
If Err.Number<>0 Then Fail "COPY PLC source"
solution.SaveAs fso.BuildPath(outdir,"FCM_Homing_Offline.sln")
If Err.Number<>0 Then Fail "Save isolated solution"
Set sb=solution.SolutionBuild
If Err.Number<>0 Then Fail "SolutionBuild"
WScript.Echo "Building imported PLC source..."
sb.Build True
If Err.Number<>0 Then Fail "Build"
last=sb.LastBuildInfo
WScript.Echo "LastBuildInfo=" & last
Err.Clear
Set errors=dte.Windows.Item("{D78612C7-9962-4B83-95D9-268046DAD23A}").Object.ErrorItems
If Err.Number=0 Then
    For i=1 To errors.Count
        Set item=errors.Item(i)
        WScript.Echo item.ErrorLevel & " " & item.FileName & ":" & item.Line & " " & item.Description
    Next
End If
Err.Clear
Set panes=dte.Windows.Item("{34E76E81-EE4A-11D0-AE2E-00A0C90FFFC3}").Object.OutputWindowPanes
If Err.Number=0 Then
    For i=1 To panes.Count
        Set pane=panes.Item(i)
        Set ep=pane.TextDocument.StartPoint.CreateEditPoint()
        outputText=ep.GetText(pane.TextDocument.EndPoint)
        If InStr(outputText,"Build started")>0 Then WScript.Echo outputText
    Next
End If
Err.Clear
dte.Quit
If last<>0 Then WScript.Quit 2
WScript.Echo "OFFLINE_BUILD_REPORTED_OK: " & outdir
