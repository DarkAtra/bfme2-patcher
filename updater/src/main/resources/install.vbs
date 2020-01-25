Dim oFso : Set oFso = CreateObject("Scripting.FileSystemObject")
Dim attempts
attempts = 0

On Error Resume Next

do
    Err.Clear
    oFso.DeleteFile "updater.jar"
    oFso.MoveFile "_updater.jar", "updater.jar"

    if Err.Number = 0 then
        Dim objShell
        Set objShell = WScript.CreateObject( "WScript.Shell" )
        objShell.Run("""updater.jar""")
        WScript.Quit 0
    end if

    attempts = attempts + 1
    WScript.Sleep 500
loop while attempts < 20

On Error Goto 0
