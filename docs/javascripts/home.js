document$.subscribe(() => {
  const home = document.querySelector("a.md-header__button.md-logo");
  if (!home) return;

  const targets = document.querySelectorAll(
    ".md-header__title, .md-sidebar--primary .md-nav--primary > .md-nav__title"
  );
  for (const target of targets) {
    target.classList.add("home-link");
    target.onclick = (event) => {
      if (event.target.closest("a, button")) return;
      event.preventDefault();
      home.click();
    };
  }
});
