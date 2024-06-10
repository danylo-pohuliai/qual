let charts = {}
let serverIP = 'http://192.168.1.5:5000'
let espIP = 'http://192.168.1.2'

const formatDateTimeLocal = (date) => {
  const pad = (num) => num.toString().padStart(2, '0')
  const year = date.getFullYear()
  const month = pad(date.getMonth() + 1)
  const day = pad(date.getDate())
  const hours = pad(date.getHours())
  const minutes = pad(date.getMinutes())

  return `${year}-${month}-${day}T${hours}:${minutes}`
}

const onPanZoomStart = () => {
  this.stop()
}

const circuitNames = ['VA','VB','VC','IA','IB','IC']

const labels = [
  " Напруга на фазі А, вольт",
  " Напруга на фазі В, вольт",
  " Напруга на фазі С, вольт",
  " Струм на фазі А, ампер",
  " Струм на фазі B, ампер",
  " Струм на фазі C, ампер"
]

const createLineChart = (xData, yData, context, label) => {
  if (charts[context.canvas.id]) {
    charts[context.canvas.id].destroy();
  }
  let gradient = context.createLinearGradient(0, 0, 0, window.screen.width / 2)
  gradient.addColorStop(0, 'rgba(74, 169, 230, 0.8)')
  gradient.addColorStop(1, 'rgba(74, 169, 230, 0.001)')
  let data = {
    labels: xData,
    datasets: [{
      label: label,
      data: yData,
      pointStyle: false,
      fill: true,
      backgroundColor: gradient,
      borderWidth: 2,
      borderColor: 'rgba(74, 169, 230, 1)',
      tension: 0.2
    }]
  }
  let xScaleConfig = {
    min: 0,
    max: xData.length,
    ticks: {
      autoSkip: true,
      maxRotation: 0,
      color: 'rgba(74, 169, 230, 0.9)'
    },
    border: {
      color: 'rgba(74, 169, 230, 1)'
    },
    grid: {
      color: 'rgba(74, 169, 230, 0.3)'
    }
  }
  let yScaleConfig = {
    ticks: {
      color: 'rgba(74, 169, 230, 0.9)'
    },
    border: {
      color: 'rgba(74, 169, 230, 1)'
    },
    grid: {
      color: 'rgba(74, 169, 230, 0.3)'
    }
  }
  let zoomOptions = {
    pan: {
      enabled: true,
      mode: 'x',
      onPanStart: onPanZoomStart
    },
    zoom: {
      mode: 'x',
      pinch: {
        enabled: true
      },
      wheel: {
        enabled: true
      },
      limits: {
        x: {min: 0, max: xData.length}
      },
      onZoomStart: onPanZoomStart
    }
  }
  let config = {
    type: 'line',
    data: data,
    options: {
      scales: {
        x: xScaleConfig,
        y: yScaleConfig
      },
      plugins: {
        legend: {
          display: false
        },
        zoom: zoomOptions
      },
      animation: {
        duration: 400,
        easing: 'linear',
        y: {
          fn: (from, to, factor) => to
        }
      }
    }
  }
  let chart = new Chart(context, config)
  charts[context.canvas.id] = chart
  return chart
}

const createChart = async (context, path, label, from, to) => {
  try {
    const response = await $.ajax({
      url: path,
      method: 'GET',
      data: {
          from: from,
          to: to
      }
    })
      let data = response.data
      let xData = []
      let yData = []
      let keys
      if (data.length == 0) {
        return canvasError('Дані за цей період відсутні', context)
      } else {
        keys = Object.keys(data[0])
      }
      for (let i = 0; i <= data.length - 1; i++) {
        let unixTimestamp = data[i].timestamp - (60 * 60 * 3)
        let date = new Date(unixTimestamp * 1000)
        let month = ('0' + (date.getMonth() + 1)).slice(-2)
        let day = ('0' + date.getDate()).slice(-2)
        let hours = ('0' + date.getHours()).slice(-2)
        let minutes = ('0' + date.getMinutes()).slice(-2)
        let seconds = ('0' + date.getSeconds()).slice(-2)
        let formattedDateTime = `${day}-${month} ${hours}:${minutes}:${seconds}`
        xData.push(formattedDateTime)
        yData.push(data[i][keys[0]])
      }
      let xStartData = []
      let yStartData = []
      let xParseData = []
      let yParseData = []
      console.log(data.length)
      for (let i = 0; i < xData.length; i++) {
          if (i < 50) {
              xStartData.push(xData[i])
              yStartData.push(yData[i])
          } else {
              xParseData.push(xData[i])
              yParseData.push(yData[i])
          }
      }

      let chart = createLineChart(xData, yData, context, label)
      chart.update()
      
      console.log(response)

      return chart
    
  } catch (error) {
        console.error(error)
        return canvasError(error, context)
      }
}

const canvasError = (text, context) => {
  context.clearRect(0, 0, context.canvas.width, context.canvas.height)
  context.canvas.width = 500
  context.font = '24px Arial'
  context.fillStyle = 'red'
  context.textAlign = 'center'
  context.fillText('Помилка: ' + text, context.canvas.width / 2, context.canvas.height / 2)
  return context
}

const updateSettings = async () => {
  let dots = ''
  let intervalId
  $('.connection h1').html('Підключення до ESP<span class="dots">...</span>')
  
  $('.connection').show()
  $('.updateButton').hide()
  dots = ''
  intervalId = setInterval(function() {
    dots = dots.length < 3 ? dots + '.' : ''
    $('.dots').text(dots)
  }, 500)

  try {
    const response = await $.ajax({
      url: serverIP + '/espSettings',
      method: 'GET'
    })
    $('#APPassword').val(response.APpass)
    $('#APSSid').val(response.APssid)
    $('#APlocalIP').val(response.APlocalIP)
    $('#SendToServ').prop('checked', response.sendToServ)
    $('#WriteSD').prop('checked', response.writeSD) 
    $('#stopMeasurements').prop('checked', response.stopMeasurements) 
    $('#syncMeasurements').prop('checked', response.syncMeasurements)
    $('#SSid').val(response.netSSid)
    $('#Password').val(response.netPassword)
    $('#LocalIP').val(response.localIP)
    $('#serverName').val(response.serverName)
    $('#Vlimit').val(response.Vlimit)
    $('#Ilimit').val(response.Ilimit)
    $('.settings').show()
    clearInterval(intervalId)
    $('.connection').hide()

  } catch (error) {
    console.error(error)
    clearInterval(intervalId)
    $('.connection h1').text('Помилка: ' + error.responseText)
    $('.updateButton').show()
    $('.settings').hide()
  }
}

const updateState = async () => {
  let dots = ''
  let intervalId
  $('.connection h1').html('Підключення до ESP<span class="dots">...</span>')
  
  $('.connection').show()
  $('.updateButton').hide()
  dots = ''
  intervalId = setInterval(function() {
    dots = dots.length < 3 ? dots + '.' : ''
    $('.dots').text(dots)
  }, 500)
  try {
    const response = await $.ajax({
      url: serverIP + '/espState',
      method: 'GET'
    })
    $('#light').prop('checked', response.light)
      if(response.SDcard){
          $("#statusSD").text("Працює штатно")
      } else {
          $("#statusSD").text("Помилка SD карти")
        }
    $('.settings').show()
    clearInterval(intervalId)
    $('.connection').hide()

  } catch (error) {
    console.error(error)
    clearInterval(intervalId)
    $('.connection h1').text('Помилка: ' + error.responseText)
    $('.settings').hide()
    $('.updateButton').show()

  }
}

$(document).ready(function(){

  const now = new Date()
  const currentTime = Math.floor(now.getTime() / 1000)
  const startOfDay = new Date(now.getFullYear(), now.getMonth(), now.getDate())
  const unixStartOfDay = Math.floor(startOfDay.getTime() / 1000)
  const from = unixStartOfDay + 60*60*3
  const to = currentTime + 60*60*3

  const createChartsSequentially = async (index = 0) => {
    if (index >= circuitNames.length) return

    const context = document.getElementById('canvas' + circuitNames[index]).getContext('2d')
    try {
      await createChart(context, serverIP + '/database/phase' + circuitNames[index], labels[index], from, to)
    } catch (error) {
      console.error('Помилка при створенні графіка:', error)
    }
    await createChartsSequentially(index + 1)
  }

  createChartsSequentially()
  for (let i = 0; i < circuitNames.length; i++){
    document.getElementById('startDateTime' + circuitNames[i]).value = formatDateTimeLocal(startOfDay)
    document.getElementById('endDateTime' + circuitNames[i]).value = formatDateTimeLocal(now)
  }

  updateSettings()
  updateState()

  $('.submitButton').on('click', function() {
    const circuit = $(this).data('circuit')
    const startDateTimeInput = document.getElementById('startDateTime' + circuit).value
    const endDateTimeInput = document.getElementById('endDateTime' + circuit).value
  
    const from = Math.floor(new Date(startDateTimeInput).getTime() / 1000) + 60*60*3
    const to = Math.floor(new Date(endDateTimeInput).getTime() / 1000) + 60*60*3
    let context = document.getElementById('canvas' + circuit).getContext('2d')
    context.clearRect(0, 0, context.canvas.width, context.canvas.height)
    console.log(labels[circuitNames.indexOf(circuit)])
    try {
      createChart(context, serverIP + '/database/phase' + circuit, labels[circuitNames.indexOf(circuit)], from, to)
    } catch (error) {
      console.error('Помилка при створенні графіка:', error)
    }
  })
  
  $('.updateButton').on('click', function() {
    updateSettings()
    updateState()
  })
  
  $("#saveChanges").click(function(e){
    e.preventDefault()
    let settings = {
        netSSid: $('#SSid').val(),
        netPassword: $('#Password').val(),
        localIP: $('#LocalIP').val(),
        sendToServ: $('#SendToServ').prop('checked'),
        writeSD: $('#WriteSD').prop('checked'),
        stopMeasurements: $('#stopMeasurements').prop('checked'),
        syncMeasurements: $('#syncMeasurements').prop('checked'),
        APssid: $('#APSSid').val(),
        APpass: $('#APPassword').val(),
        APlocalIP: $('#APlocalIP').val(),
        serverName: $('#serverName').val(),
        Vlimit: parseInt($('#Vlimit').val()),
        Ilimit: parseInt($('#Ilimit').val())
    }
    let jsonData = JSON.stringify(settings)
    console.log(settings)
  
    $.ajax({
        url: serverIP + '/espChangeSettings', 
        type: 'POST', 
        contentType: 'application/json', 
        data: jsonData, 
        success: function(response) {
            alert("Налаштування змінені успішно!!")
            console.log(response)
        },
        error: function(xhr, status, error) {
            alert(error)
            console.error(error)
        }
    })
  })

  $('#light').change(function(e){
    e.preventDefault()
    if(this.checked) {
        $.get('/light?state=' + 1, function(data){
          console.log(data)
          alert(data)
        })
    } else {
        $.get('/light?state=' + 0, function(data){
          console.log(data)
          alert(data)
        })
    }
})
})


