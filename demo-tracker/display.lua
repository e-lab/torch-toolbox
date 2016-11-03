--------------------------------------------------------------------------------
-- FixedCam: complete vision system processing for fixed cameras
--
-- E. Culurciello, August 2015-2016
--------------------------------------------------------------------------------

local display = {} -- main display object

local numericcolours = {
   0xffffff, 0x0000ff, 0x00ff00, 0x008000, 0xff8000, 0x00ffff,
   0x008080, 0xff00ff, 0x800080, 0x8000ff, 0x808000,
   0x808080, 0x404040, 0xff0000, 0x800000, 0xffff00, 0x808000
}

local colours = {
   'white', 'blue', 'green', 'darkGreen', 'orange', 'cyan',
   'darkCyan', 'magenta', 'darkMagenta', 'purple', 'brown',
   'gray', 'darkGray', 'red', 'darkRed', 'yellow', 'darkYellow'
}

local function prep_display(opt, source, class_names, target_list)
   local classes = class_names
   local targets = target_list
   local zoom = opt.zoom
   local eye = opt.is
   local wdt = opt.wdt
   local diw = opt.diw
   local threshold = opt.pdt
   local loop_idx = 0
   local mean_idx = 0

   local fps_avg = torch.FloatTensor(diw):zero()
   local side = math.min(source.h, source.w)
   local win_w = zoom * source.w
   local win_h = zoom * source.h
   local cam = nil
   local wzoom, hzoom

   if opt.livecam then
      cam = opt.input == 'geocam' and require 'geolivecam' or require 'livecam'
      if opt.fullscreen then
         win_w, win_h = cam.window('Camera detector')
      else
         win_w, win_h = cam.window('Camera detector', 0, 0, win_w, win_h)
      end
      wzoom = win_w / source.w
      hzoom = win_h / source.h
   else
      win = qtwidget.newwindow(win_w, win_h, 'Camera detector')
      win:setfontsize(zoom*20)
   end

   display.forward = function(img, objects, fps)
      results = results or {}

      mean_idx = mean_idx + 1
      if mean_idx > diw then
         mean_idx = 1
      end
      fps_avg[mean_idx] = fps or 0

      if cam then
         cam.clear()
         if opt.loglevel > 1 then
            cam.text(15, 25, string.format('%.2f fps', torch.mean(fps_avg)), 20, 0xffffffff)
         end
      else
         win:gbegin()
         win:showpage()

         -- display frame, face image, tracked image:
         image.display{image = img, win = win, zoom = zoom}

         if opt.loglevel > 1 then
            -- rectangle for text:
            win:rectangle(0, 0, zoom*100, 35)
            win:setcolor(0, 0, 0, 0.4)
            win:fill()

            -- report fps
            win:moveto(15, 25)
            win:setcolor('white')
            win:show(string.format('%.2f fps', torch.mean(fps_avg)))
         end
      
      end
      -- visualizing predictions
      for i, o in ipairs(objects) do
         local d = o.coor

         if o.faceImg and not cam then
            image.display{image = o.faceImg, win = win, zoom = zoom, x=win_w-o.faceImg:size(3)-10, y=zoom*10}
            win:setcolor('green')
            win:setlinewidth(zoom*4)
            win:rectangle(zoom*(win_w-o.faceImg:size(3)-10), zoom*10, zoom*o.faceImg:size(3), zoom*o.faceImg:size(2))
            win:stroke()
         end
         -- if o.trackImg then
            -- win:moveto(source.w-o.trackImg:size(3), source.h-o.trackImg:size(2))
            -- image.display{image = o.trackImg, win = win, zoom = zoom}
         -- end

         if o.target then
            -- plot blobs of target:
            -- outer rectangle
            if cam then
               if not o.colour then
                  o.colour = colours[math.random(#colours)]
               end
               cam.rectangle(wzoom*d.x1, hzoom*d.y1, wzoom*d.dx, hzoom*d.dy, zoom*8, o.colour)
               -- text:
               if o.faceid then
                  cam.text(wzoom*(d.x1+10), hzoom*(d.y1+30), o.faceid, 20, o.colour)
                  cam.text(wzoom*(win_w-o.faceImg:size(3)), hzoom*30, o.faceid20, o.colour)
               else
                  local text = classes[o.class]
                  if opt.loglevel > 1 then text = text..' '..o.id[1] end
                  cam.text(wzoom*(d.x1+10), hzoom*(d.y1+10), text, 20, o.colour)
               end
            else
               if not o.colour then
                  o.colour = colours[math.random(#colours)]
               end
               win:setlinewidth(zoom*8)
               win:setcolor(o.colour)
               win:rectangle(zoom*d.x1, zoom*d.y1, zoom*d.dx, zoom*d.dy)
               win:stroke()
               -- text:
               win:moveto(zoom*(d.x1+10), zoom*(d.y1+30))
               if o.faceid then
                  win:show(o.faceid)
                  win:moveto(zoom*(win_w-o.faceImg:size(3)), zoom*30)
                  win:show(o.faceid)
               else
                  local text = classes[o.class]
                  if opt.loglevel > 1 then text = text..' '..o.id[1] end
                  win:show(text)
               end
            end
         else
            -- show as thin gray rectangle:

            if cam then
               cam.rectangle(wzoom*d.x1, hzoom*d.y1, wzoom*d.dx, hzoom*d.dy, zoom*2, 0xff808080)
            else
               win:setlinewidth(zoom*2)
               win:setcolor('gray')
               win:rectangle(zoom*d.x1, zoom*d.y1, zoom*d.dx, zoom*d.dy)
               win:stroke()
            end
         end

      end
      if not opt.livecam then
         win:gend()
      end
   end

      -- set screen grab function
   display.screen = function()
      return win and win:image() or nil
   end

   -- set continue function
   display.continue = function()
      return win and win:valid() or true
   end

   -- set close function
   display.close = function()
      if win and win:valid() then
         win:close()
      end
   end
end



function display:init(opt, source, class_names, target_list, custom_colours)
   assert(opt and ('table' == type(opt)))
   assert(source and ('table' == type(source)))
   assert(class_names and ('table' == type(class_names)))

   if not opt.gui then
      prep_no_gui(opt, source, class_names, target_list)
   else
      if opt.livecam then
         colours = numericcolours
      else
         require 'qtwidget'
      end
      colours = custom_colours or colours
      local colours_length = #colours
      for i=1, (#class_names) do
         -- replicate colours for extra categories
         local index = (i % colours_length == 0) and colours_length or i%colours_length
         colours[colours_length+i] = colours[index]
      end

      prep_display(opt, source, class_names, target_list)

   end
end


return display
