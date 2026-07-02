const cards = document.querySelectorAll(".card");
const architectureBlocks = document.querySelectorAll(".architecture div");
const statusBoxes = document.querySelectorAll(".status-box span");

function addHoverEffect(elements, hoverColor, normalColor) {
  elements.forEach(element => {
    element.addEventListener("mouseenter", () => {
      element.style.background = hoverColor;
    });

    element.addEventListener("mouseleave", () => {
      element.style.background = normalColor;
    });
  });
}

addHoverEffect(cards, "#334155", "#1e293b");
addHoverEffect(architectureBlocks, "#334155", "#1e293b");
addHoverEffect(statusBoxes, "#334155", "#1e293b");

console.log("Intelligent Embedded HMI Website Loaded Successfully");
