document.addEventListener('DOMContentLoaded', () => {
 // Navigation Active Link
const navLinks = document.querySelectorAll('.nav-links a');
const sections = document.querySelectorAll('section');
const mobileMenu = document.getElementById('mobile-menu');
const navLinksContainer = document.querySelector('.nav-links');

// Mobile Menu Toggle
if (mobileMenu) {
    mobileMenu.addEventListener('click', () => {
        mobileMenu.classList.toggle('active');
        navLinksContainer.classList.toggle('active');
    });
}

// Close mobile menu when a link is clicked
navLinks.forEach(link => {
    link.addEventListener('click', () => {
        if (navLinksContainer.classList.contains('active')) {
            mobileMenu.classList.remove('active');
            navLinksContainer.classList.remove('active');
        }
    });
});

window.addEventListener('scroll', () => {
        let current = '';
        
        sections.forEach(section => {
            const sectionTop = section.offsetTop;
            const sectionHeight = section.clientHeight;
            // Offset logic for sticky header
            if (pageYOffset >= (sectionTop - 150)) {
                current = section.getAttribute('id');
            }
        });

        navLinks.forEach(link => {
            link.classList.remove('active');
            if (link.getAttribute('href').includes(current)) {
                link.classList.add('active');
            }
        });
    });
});
