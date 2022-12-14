// typewriter text
const typewriter_text = ["Miniature Gaming Console", "By: Soumyarup Lahiri (sl2625), Tiffany Pang (txp2), Prishita Ray (pr376)"];

// responsive navigation
const navSlide = () => {
	const burger = document.querySelector('.burger');
	const nav = document.querySelector('.nav-links');
	const navlinks = document.querySelectorAll('.nav-links li');

	burger.addEventListener('click', () => {
		// toggle nav
		nav.classList.toggle('nav-active');
	
		// animate links
		navlinks.forEach((link, index) => {
			if (link.style.animation) {
				link.style.animation = '';
			} else {
				link.style.animation = `navlinkFade 0.5s ease forwards ${index / 7 + 0.5}s`;
			}
		});
		
		// animate burger
		burger.classList.toggle('toggle');
	});
}

// sleep function
function sleep(milliseconds) {
    let timeStart = new Date().getTime();
    while (true) {
      	let elapsedTime = new Date().getTime() - timeStart;
      	if (elapsedTime > milliseconds) {
        	break;
      	}
    }
}

let textIter = 0;

// typewriter effect - text deletion
function deletingEffect() {
	let word = typewriter_text[textIter].split("");
	var loopDeleting = function() {
		if (word.length > 0) {
			word.pop();
			document.getElementById('typewriter-text').innerHTML = word.join("");
		} else {
			if (typewriter_text.length > (textIter + 1)) {
				textIter++;
			} else {
				textIter = 0;
			}

			typingEffect();
			return false;
		}

		setTimeout(loopDeleting, 150);
	}

	loopDeleting();
}

// typewriter effect - text writing
function typingEffect() {
	let word = typewriter_text[textIter].split("");
	var loopTyping = function () {
		if (word.length > 0) {
			document.getElementById('typewriter-text').innerHTML += word.shift();
		} else {
			sleep(1000);
			deletingEffect();
			return false;
		}

		setTimeout(loopTyping, 150);
	}

	loopTyping();
}

// scroll indicator
function UpdateScrollProgress() {
	var toppos = document.documentElement.scrollTop;
	var remaining = document.documentElement.scrollHeight - document.documentElement.clientHeight;

	var percentage = (toppos / remaining) * 100;

	document.getElementsByClassName('progress-bar')[0].style.width = percentage + "%";
}

// scroll event listener
document.addEventListener("scroll", () => {
	UpdateScrollProgress();
})

// calling functions
typingEffect();
navSlide();
