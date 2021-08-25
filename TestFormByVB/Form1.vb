Imports System.Reflection
Imports System.Runtime.InteropServices





Public Class Form1
    Declare Function easyPlay Lib "SimpleMediaPlayer.dll" (dwFreq As IntPtr, dwDuration As IntPtr) As Int64

    Private Sub Form1_Load(sender As Object, e As EventArgs) Handles MyBase.Load
        Dim player As IntPtr
        Dim a As Assembly = Assembly.Load("SimpleMediaPlayer")
        ' Get the type to use.
        Dim myType As Type = a.GetType("Example")
        ' Get the method to call.
        Dim result As Int64 = easyPlay(Me.Handle, Me.Handle)
        Console.WriteLine(result)
    End Sub
End Class
