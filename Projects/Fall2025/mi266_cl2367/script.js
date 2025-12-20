document.addEventListener("DOMContentLoaded", () => {
  const modeEl = document.getElementById("mode");
  const nav = document.querySelector(".section-nav");
  if (!nav) return;

  const links = Array.from(nav.querySelectorAll('a[href^="#"]'));
  const sections = Array.from(document.querySelectorAll("section[id]"));

  // Map id -> li (robust, no CSS.escape dependency)
  const idToLi = new Map();
  for (const a of links) {
    const id = decodeURIComponent(a.getAttribute("href").slice(1));
    const li = a.closest("li");
    if (id && li) idToLi.set(id, li);
  }

  function clearActive() {
    nav.querySelectorAll("li.active, li.active-parent").forEach((li) => {
      li.classList.remove("active");
      li.classList.remove("active-parent");
    });
  }

  function setActive(id) {
    clearActive();
    const li = idToLi.get(id);
    if (!li) return;

    li.classList.add("active");

    // If nested, also mark its parent <li>
    const parentLi = li.parentElement && li.parentElement.closest("li");
    if (parentLi) parentLi.classList.add("active-parent");
  }

  // Initialize
  if (location.hash) setActive(decodeURIComponent(location.hash.slice(1)));
  else if (sections[0]) setActive(sections[0].id);

  // Keep URL in sync (optional)
  links.forEach((a) => {
    a.addEventListener("click", () => {
      const id = decodeURIComponent(a.getAttribute("href").slice(1));
      if (!id) return;
      setActive(id);
      history.replaceState(null, "", "#" + encodeURIComponent(id));
    });
  });

  // Preferred: IntersectionObserver
  const canIO = "IntersectionObserver" in window;
  let ioWorked = false;

  if (canIO) {
    modeEl.textContent = "mode: IntersectionObserver";

    const io = new IntersectionObserver(
      (entries) => {
        const visible = entries
          .filter((e) => e.isIntersecting)
          .sort((a, b) => b.intersectionRatio - a.intersectionRatio);

        if (visible.length) {
          ioWorked = true;
          setActive(visible[0].target.id);
        }
      },
      {
        root: null,
        threshold: [0.15, 0.3, 0.5, 0.75],
        rootMargin: "-10% 0px -55% 0px",
      }
    );

    sections.forEach((s) => io.observe(s));

    // If IO never fires (e.g., page scrolls inside a container), fall back
    setTimeout(() => {
      if (!ioWorked) {
        modeEl.textContent = "mode: Scroll fallback (IO did not fire)";
        enableScrollFallback();
      }
    }, 800);
  } else {
    modeEl.textContent = "mode: Scroll fallback (no IO support)";
    enableScrollFallback();
  }

  // Fallback: scroll-based spy
  function enableScrollFallback() {
    const spy = () => {
      const refY = window.innerHeight * 0.25;
      let best = null;
      let bestDist = Infinity;

      for (const s of sections) {
        const r = s.getBoundingClientRect();
        const dist = Math.abs(r.top - refY);
        if (r.bottom > 0 && r.top < window.innerHeight && dist < bestDist) {
          best = s;
          bestDist = dist;
        }
      }
      if (best) setActive(best.id);
    };

    window.addEventListener("scroll", spy, { passive: true });
    window.addEventListener("resize", spy);
    spy();
  }
});
