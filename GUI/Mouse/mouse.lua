-- Alfredo Canziani, Dec 14

-- Requires
require 'qtwidget'
require 'qtuiloader'

-- Setup GUI (external UI file)
if not win or not widget then
   widget = qtuiloader.load('mouse.ui')
   win = qt.QtLuaPainter(widget.canvas)
end

-- Setup mouse connectivity
qt.connect(qt.QtLuaListener(widget.canvas),
'sigMouseMove(int,int,QByteArray,QByteArray)',
--'sigMousePress(int,int,QByteArray,QByteArray,QByteArray)',
function (x,y)
   widget.label.text = x .. ', ' .. y
end)

-- Turn on GUI
widget:show()
