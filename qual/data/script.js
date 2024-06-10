
$(document).ready(function(){
    let currentPage = 1
    const pageSize = 25
    let totalFiles = 0
    let totalPages = 0
    let folderName = "UnSyncMe"

    function fetchConfig() {
        $.getJSON("/config.json") 
            .done (function(data) {
                $('#APPassword').val(data.APpass)
                $('#APSSid').val(data.APssid)
                $('#APlocalIP').val(data.APlocalIP)
                $('#SendToServ').prop('checked', data.sendToServ)
                $('#WriteSD').prop('checked', data.writeSD) 
                $('#stopMeasurements').prop('checked', data.stopMeasurements) 
                $('#syncMeasurements').prop('checked', data.syncMeasurements)
                $('#SSid').val(data.netSSid)
                $('#Password').val(data.netPassword)
                $('#LocalIP').val(data.localIP)
                $('#serverName').val(data.serverName)
                $('#Vlimit').val(data.Vlimit)
                $('#Ilimit').val(data.Ilimit)
        })
        .fail(function(xhr, status, error){
            alert(xhr.status + " - " + xhr.responseText)
        })
    }
    function fetchState() {
        $.getJSON("/state.json")
            .done(function(data) {
                $('#light').prop('checked', data.light)
                if(data.SDcard){
                    $("#statusSD").text("Працює штатно")
                } else {
                    $("#statusSD").text("Помилка SD карти")
                }
                $("#unSyncAmount").text(data.unSyncFilesAmount)
                totalFiles = data.unSyncFilesAmount
                totalPages = Math.ceil(totalFiles / pageSize)
                updatePaginationButtons()
            })
            .fail(function(xhr, status, error){
                alert(xhr.status + " - " + xhr.responseText)
            })
    }

    function createTable(name, filesList){
        const tableBody = $('#' + name)
        tableBody.empty()
            filesList.forEach(file => {
                const row = $('<tr></tr>')
        
                const nameCell = $('<td></td>').text(file)
                row.append(nameCell)
        
                const actionCell = $('<td class="actions"></td>').html(`
                    <button class="btn btn-download">Download</button>
                    <button class="btn btn-delete">Delete</button>
                `)
                row.append(actionCell)
        
                tableBody.append(row)
                })
        addEventListeners()
    }

    function fetchSDfilesList(from, to, folder) {
        $.getJSON('/filesList?from=' + from + '&to=' + to + '&folder=' + folder) 
        .done(function(data){
            let filesList = data.data
            createTable('unSyncFileTableBody', filesList)
        })
        .fail(function(xhr, status, error){
            alert(xhr.status + " - " + xhr.responseText)
        })
    }

    function updatePaginationButtons() {
        const pagination = $('.pagination')
        pagination.empty()

        const startPage = Math.max(1, currentPage - 1)
        const endPage = Math.min(totalPages, currentPage + 1)

        if (startPage > 1) {
            pagination.append(`<button class="page-btn" data-page="1">1</button>`)
            if (startPage > 2) {
                pagination.append(`<span>...</span>`)
            }
        }

        for (let i = startPage; i <= endPage; i++) {
            const isActive = i === currentPage
            const button = $('<button></button>').text(i).toggleClass('active', isActive).addClass('page-btn')
            button.click(function() {
                currentPage = i
                const pagination = $('.pagination')
                pagination.empty()
                fetchSDfilesList((currentPage - 1) * pageSize + 1, currentPage * pageSize, folderName)
                fetchState()
                alert("Поточна сторінка: " + i) 
            })
            pagination.append(button)
        }

        if (endPage < totalPages) {
            if (endPage < totalPages - 1) {
                pagination.append(`<span>...</span>`)
            }
            pagination.append(`<button class="page-btn" data-page="${totalPages}">${totalPages}</button>`)
        }

        pagination.find('button[data-page="1"]').off('click').click(function() {
            currentPage = 1
            const pagination = $('.pagination')
            pagination.empty()
            fetchSDfilesList(1, pageSize, folderName)
            fetchState()
            alert("Поточна сторінка: " + currentPage) 
        })
    
        pagination.find('button[data-page="' + totalPages + '"]').off('click').click(function() {
            currentPage = totalPages
            const pagination = $('.pagination')
            pagination.empty()
            fetchSDfilesList((totalPages - 1) * pageSize + 1, totalPages * pageSize, folderName)
            fetchState()
            alert("Поточна сторінка: " + currentPage) 
        })
    }

    function downloadSDfile(path){
        $.get('/getSDfile?path=' + path) 
        .done(function(data){
            let blob = new Blob([data], { type: 'text/plain' });
            let link = document.createElement('a');
            link.href = window.URL.createObjectURL(blob);
            link.download = path;
            link.click();
        })
        .fail(function(xhr, status, error){
            alert(xhr.status + " - " + xhr.responseText)
        })
    }

    function deleteSDfile(path){
        $.get('/deleteSDfile?path=' + path) 
        .done(function(data){
            alert(data)
            return true
        })
        .fail(function(xhr, status, error){
            alert(xhr.status + " - " + xhr.responseText)
            return false
        })
    }

    const addEventListeners = () => {
        $('.btn-download').off('click').on('click', function() {
            const fileName = $(this).closest('tr').find('td:first').text()
            let path = "UnSyncMe/" + fileName
            const confirmed = confirm('Ви дійсно хочете завантажити ' + path + '?')
            if (confirmed) {
                downloadSDfile(path)
            }    
        })

        $('.btn-delete').off('click').click(function() {
            const fileName = $(this).closest('tr').find('td:first').text()
            const row = $(this).closest('tr')
            let path, amount
            path = folderName + "/" + fileName
            amount = $('#unSyncAmount')
            const confirmed = confirm('Ви дійсно хочете видалити ' + path + '?')
            if (confirmed) {
                deleteSDfile(path)
                row.remove()
                prevAmount = parseInt(amount.text(),10)
                prevAmount--
                amount.text(prevAmount)
            }      
        })
    }

    fetchState()
    fetchConfig()
    fetchSDfilesList(1, pageSize, folderName)

    $('#light').change(function(e){
        e.preventDefault()
        if(this.checked) {
            $.get('/light?state=1', function(data){alert(data)})
        } else {
            $.get('/light?state=0', function(data){alert(data)})
        }
    })
    
    $("#saveChanges").click(function(e){
        e.preventDefault()
        console.log("prikol")
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
        console.log(jsonData)

        $.ajax({
            url: '/settings', 
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

    $('#firstPage').click(() => {
        currentPage = 1
        fetchSDfilesList(1, pageSize, folderName)
        updatePaginationButtons()
    })

    $('#prevPage').click(() => {
        if (currentPage > 1) {
            currentPage--
            fetchSDfilesList((currentPage - 1) * pageSize + 1, currentPage * pageSize, folderName)
            updatePaginationButtons()
        }
    })

    $('#nextPage').click(() => {
        if (currentPage < totalPages) {
            currentPage++
            fetchSDfilesList((currentPage - 1) * pageSize + 1, currentPage * pageSize, folderName)
            updatePaginationButtons()
        }
    })

    $('#lastPage').click(() => {
        currentPage = totalPages
        fetchSDfilesList((currentPage - 1) * pageSize + 1, currentPage * pageSize, folderName)
        updatePaginationButtons()
    })
})

window.onload = function() {
    $.get('/meas?state=0', function(data){alert(data)})
}