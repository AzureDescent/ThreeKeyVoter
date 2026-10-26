// 1. Серверээс өгөгдөл татах функц
async function fetchVoteData() {
  try {
    // Zhaoyan-ийн Backend сервер рүү хүсэлт явуулах
    const response = await fetch('http://127.0.0.1:8000/state');

    // Хэрэв сервер унтарсан байвал алдаа шидэх
    if (!response.ok) throw new Error("Сервер ажиллахгүй байна");

    const data = await response.json();

    // 2. Өгөгдлүүдийг салгаж авах
    const total = data.total || 0;
    const onlineDevices = data.devices_online || 0;
    const yesVotes = data.responses.understand || 0;
    const notSureVotes = data.responses.unsure || 0;
    const noVotes = data.responses.not_understand || 0;

    // 3. Нийт тоо болон төхөөрөмжийн тоог шинэчлэх
    document.getElementById('total-votes').innerText = total;
    document.getElementById('donut-total').innerText = total;
    document.getElementById('device-count').innerText = onlineDevices;

    // 4. Картууд дээрх тоонуудыг шинэчлэх
    document.getElementById('yes-count').innerText = yesVotes;
    document.getElementById('notsure-count').innerText = notSureVotes;
    document.getElementById('no-count').innerText = noVotes;

    // 5. Хувь (Percent) бодох
    if (total > 0) {
      const yesPercent = Math.round((yesVotes / total) * 100);
      const notSurePercent = Math.round((notSureVotes / total) * 100);
      const noPercent = Math.round((noVotes / total) * 100);

      // Хувийг текстээр харуулах
      document.getElementById('yes-percent').innerText = yesPercent + '%';
      document.getElementById('notsure-percent').innerText = notSurePercent + '%';
      document.getElementById('no-percent').innerText = noPercent + '%';

      document.getElementById('legend-yes').innerText = yesPercent + '%';
      document.getElementById('legend-notsure').innerText = notSurePercent + '%';
      document.getElementById('legend-no').innerText = noPercent + '%';

      // 6. Доод талын багануудыг (Progress bar) өргөсгөх
      document.getElementById('yes-bar').style.width = yesPercent + '%';
      document.getElementById('notsure-bar').style.width = notSurePercent + '%';
      document.getElementById('no-bar').style.width = noPercent + '%';
    }

    // 7. Сервер ажиллаж байгаа тул "Live" тэмдэглэгээг ногоон болгох
    const liveBadge = document.getElementById('live-badge');
    if (liveBadge) liveBadge.classList.remove('offline');

  } catch (error) {
    console.error("Сервертэй холбогдоход алдаа гарлаа:", error);

    // Хэрэв сервер унтарсан бол "Live" тэмдэглэгээг улаан болгох
    const liveBadge = document.getElementById('live-badge');
    if (liveBadge) liveBadge.classList.add('offline');
  }
}

// 8. Секунд тутамд серверээс мэдээлэл татаж байх
setInterval(fetchVoteData, 1000);

// Хуудас ачаалмагц шууд нэг удаа татах
fetchVoteData();
