
-- global ui object
local ui = {}

-- setup gui
local timer = qt.QTimer()
timer.interval = 10
timer.singleShot = true
timer:start()
qt.connect(timer,
           'timeout()',
           function()
              controls(ui)
              getFrame(ui)
              process(ui)
              display(ui)
              timer:start()
           end)
ui.timer = timer

-- connect all buttons to actions
ui.classes = {widget.pushButton_1, widget.pushButton_2, widget.pushButton_3, 
               widget.pushButton_4, widget.pushButton_5}

ui.progBars = {widget.progressBar_1, widget.progressBar_2, widget.progressBar_3, 
               widget.progressBar_4, widget.progressBar_5}

ui.objLabels = {widget.label_1, widget.label_2, widget.label_3, 
                widget.label_4, widget.label_5}

-- colors
ui.colors = {'blue', 'green', 'orange', 'cyan', 'purple', 'brown', 'gray', 'red', 'yellow'}

-- set current class to learn
ui.currentId = 1
ui.currentClass = ui.classes[ui.currentId].text:tostring()

function ui.resetProgBars()
  -- init progressBars to dist of 100:
  for i=1,#ui.progBars do
    ui.progBars[i].value = 100
  end
end

function ui.resetObjLabels()
  -- init progressBars to dist of 100:
  for i=1,#ui.objLabels do
    ui.objLabels[i].text = 0
  end
end

ui.resetProgBars()

-- reset
qt.connect(qt.QtLuaListener(widget.pushButton_forget),
           'sigMousePress(int,int,QByteArray,QByteArray,QByteArray)',
           function (...)
              ui.forget = true
           end)

-- learn new prototype
ui.learn = false
for i,button in ipairs(ui.classes) do
   qt.connect(qt.QtLuaListener(button),
              'sigMousePress(int,int,QByteArray,QByteArray,QByteArray)',
              function (...)
                 ui.currentId = i
                 ui.learn = true
              end)
end

-- mouse is used only on large GUI demo:
if opt.largegui then
  -- connect mouse pos
  widget.frame.mouseTracking = true
  qt.connect(qt.QtLuaListener(widget.frame),
             'sigMouseMove(int,int,QByteArray,QByteArray)',
             function (x,y)
                ui.mouse = {x=x,y=y}
             end)

  -- issue learning request
  ui.learnmouse = false
  qt.connect(qt.QtLuaListener(widget),
             'sigMousePress(int,int,QByteArray,QByteArray,QByteArray)',
             function (...)
                if ui.mouse then
                   ui.learngui = {x=ui.mouse.x, y=ui.mouse.y, id=ui.currentId}
                   ui.learnmouse = true
                end
             end)
end 
-- save session
ui.save = false
qt.connect(qt.QtLuaListener(widget.pushButton_save),
           'sigMousePress(int,int,QByteArray,QByteArray,QByteArray)',
           function (...)
              ui.save = true
          end)

-- load session
ui.load = false
qt.connect(qt.QtLuaListener(widget.pushButton_load),
           'sigMousePress(int,int,QByteArray,QByteArray,QByteArray)',
           function (...)
              ui.load = true
          end)

widget.windowTitle = 'Visual Learner'
widget:show()

-- provide log
ui.log = {}
ui.logit = function(str, color) table.insert(ui.log,{str=str, color=color or 'black'}) end

-- return ui
return ui
