set fs  = CreateObject("Scripting.FileSystemObject")
set ws  = WScript.CreateObject("WScript.Shell")

set link = ws.CreateShortcut("updater.lnk")
    link.TargetPath = fs.BuildPath(ws.CurrentDirectory, "updater-0.0.1.jar")
    link.Save